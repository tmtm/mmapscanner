#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <ruby.h>
#include <ruby/io.h>

static VALUE cMmapScanner;
static VALUE cMmap;

typedef struct {
    char *ptr;
    size_t size;
} mmap_data_t;

static void mmap_free(mmap_data_t *data)
{
    if (data->ptr)
        munmap(data->ptr, data->size);
    free(data);
}

static VALUE create_mmap_object(int fd, size_t offset, size_t size)
{
    mmap_data_t *data;
    void *ptr;
    if ((ptr = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, offset)) == MAP_FAILED) {
        rb_exc_raise(rb_funcall(rb_eSystemCallError, rb_intern("new"), 1, INT2FIX(errno)));
    }
    data = xmalloc(sizeof(mmap_data_t));
    data->ptr = ptr;
    data->size = size;
    return Data_Wrap_Struct(cMmap, 0, mmap_free, data);
}

static VALUE initialize(int argc, VALUE *argv, VALUE obj)
{
    VALUE src, voffset, vsize;
    size_t offset, size;
    size_t src_offset, src_size;
    VALUE src_data;

    rb_scan_args(argc, argv, "12", &src, &voffset, &vsize);
    if (voffset != Qnil && NUM2LL(voffset) < 0)
        rb_raise(rb_eRangeError, "offset out of range: %lld", NUM2LL(voffset));
    if (vsize != Qnil && NUM2LL(vsize) < 0)
        rb_raise(rb_eRangeError, "length out of range: %lld", NUM2LL(vsize));
    offset = voffset == Qnil ? 0 : NUM2SIZET(voffset);
    if (rb_obj_class(src) == cMmapScanner) {
        src_offset = NUM2SIZET(rb_iv_get(src, "offset"));
        src_size = NUM2SIZET(rb_iv_get(src, "size"));
        src_data = rb_iv_get(src, "data");
    } else if (TYPE(src) == T_FILE) {
        int fd;
        struct stat st;
        fd = RFILE(src)->fptr->fd;
        fstat(fd, &st);
        src_offset = 0;
        src_size = st.st_size;
        size = vsize == Qnil ? src_size - offset : NUM2SIZET(vsize);
        if (size > st.st_size - offset)
            size = st.st_size - offset;
        src_data = create_mmap_object(fd, offset, size);
    } else if (TYPE(src) == T_STRING) {
        src_offset = 0;
        src_size = RSTRING_LEN(src);
        src_data = src;
    } else {
        rb_raise(rb_eTypeError, "wrong argument type %s (expected File/String/MmapScanner)", rb_obj_classname(src));
    }
    if (offset >= src_size)
        rb_raise(rb_eRangeError, "length out of range: %zu >= %zu", offset, src_size);
    size = vsize == Qnil ? src_size - offset : NUM2SIZET(vsize);
    if (size > src_size - offset)
        size = src_size - offset;
    rb_iv_set(obj, "offset", SIZET2NUM(src_offset + offset));
    rb_iv_set(obj, "size", SIZET2NUM(size));
    rb_iv_set(obj, "data", src_data);
    rb_iv_set(obj, "pos", INT2NUM(0));
    rb_iv_set(obj, "matched_pos", Qnil);
    return Qnil;
}

static VALUE size(VALUE obj)
{
    return rb_iv_get(obj, "size");
}

static VALUE to_s(VALUE obj)
{
    size_t offset = NUM2SIZET(rb_iv_get(obj, "offset"));
    size_t size = NUM2SIZET(rb_iv_get(obj, "size"));
    VALUE data = rb_iv_get(obj, "data");
    mmap_data_t *mdata;

    if (TYPE(data) == T_STRING)
        return rb_str_new(RSTRING_PTR(data)+offset, size);
    Data_Get_Struct(data, mmap_data_t, mdata);
    return rb_str_new(mdata->ptr+offset, size);
}

static VALUE slice(VALUE obj, VALUE pos, VALUE len)
{
    return rb_funcall(cMmapScanner, rb_intern("new"), 3, obj, pos, len);
}

static VALUE inspect(VALUE obj)
{
    return rb_str_new2("#<MmapScanner>");
}

static VALUE pos(VALUE obj)
{
    return rb_iv_get(obj, "pos");
}

static VALUE set_pos(VALUE obj, VALUE pos)
{
    size_t p, size;

    if (NUM2LL(pos) < 0)
        rb_raise(rb_eRangeError, "out of range: %lld", NUM2LL(pos));
    p = NUM2SIZET(pos);
    size = NUM2SIZET(rb_iv_get(obj, "size"));
    if (p > size)
        rb_raise(rb_eRangeError, "out of range: %zu > %zu", p, size);
    rb_iv_set(obj, "pos", pos);
    return pos;
}

static VALUE scan_sub(VALUE obj, VALUE re, int forward, int headonly)
{
    regex_t *rb_reg_prepare_re(VALUE re, VALUE str);
    regex_t *reg;
    int tmpreg;
    int result;
    struct re_registers regs;
    size_t old_pos, matched_len;
    char *ptr;
    size_t pos, size;
    VALUE data;
    mmap_data_t *mdata;

    Check_Type(re, T_REGEXP);
    pos = NUM2SIZET(rb_iv_get(obj, "pos"));
    size = NUM2SIZET(rb_iv_get(obj, "size"));
    if (pos >= size)
        return Qnil;
    data = rb_iv_get(obj, "data");
    if (TYPE(data) == T_STRING)
        ptr = RSTRING_PTR(data);
    else {
        Data_Get_Struct(data, mmap_data_t, mdata);
        ptr = mdata->ptr;
    }
    ptr += NUM2SIZET(rb_iv_get(obj, "offset"));

    reg = rb_reg_prepare_re(re, rb_str_new("", 0));
    tmpreg = reg != RREGEXP(re)->ptr;
    if (!tmpreg) RREGEXP(re)->usecnt++;

    onig_region_init(&regs);
    if (headonly) {
        result = onig_match(reg, (UChar*)(ptr+pos),
                            (UChar*)(ptr+size),
                            (UChar*)(ptr+pos),
                            &regs, ONIG_OPTION_NONE);
    } else {
        result = onig_search(reg, (UChar*)(ptr+pos),
                             (UChar*)(ptr+size),
                             (UChar*)(ptr+pos),
                             (UChar*)(ptr+size),
                            &regs, ONIG_OPTION_NONE);
    }
    if (!tmpreg) RREGEXP(re)->usecnt--;
    if (tmpreg) {
        if (RREGEXP(re)->usecnt) {
            onig_free(reg);
        } else {
            onig_free(RREGEXP(re)->ptr);
            RREGEXP(re)->ptr = reg;
        }
    }
    if (result < 0)
        return Qnil;
    old_pos = pos;
    matched_len = regs.end[0];
    if (forward) {
        pos += matched_len;
        rb_iv_set(obj, "pos", SIZET2NUM(pos));
    }
    rb_iv_set(obj, "matched_pos", SIZET2NUM(old_pos+regs.beg[0]));
    rb_iv_set(obj, "matched_len", SIZET2NUM(regs.end[0]-regs.beg[0]));
    return rb_funcall(cMmapScanner, rb_intern("new"), 3, obj, ULL2NUM(old_pos), ULL2NUM(matched_len));
}

static VALUE scan(VALUE obj, VALUE re)
{
    return scan_sub(obj, re, 1, 1);
}

static VALUE scan_until(VALUE obj, VALUE re)
{
    return scan_sub(obj, re, 1, 0);
}

static VALUE check(VALUE obj, VALUE re)
{
    return scan_sub(obj, re, 0, 1);
}

static VALUE skip(VALUE obj, VALUE re)
{
    VALUE ret = scan_sub(obj, re, 1, 1);
    if (ret == Qnil)
        return ret;
    return rb_iv_get(ret, "size");
}

static VALUE match_p(VALUE obj, VALUE re)
{
    VALUE ret = scan_sub(obj, re, 0, 1);
    if (ret == Qnil)
        return ret;
    return rb_iv_get(ret, "size");
}

static VALUE peek(VALUE obj, VALUE size)
{
    size_t sz = NUM2SIZET(size);
    size_t data_pos = NUM2SIZET(rb_iv_get(obj, "pos"));
    size_t data_size = NUM2SIZET(rb_iv_get(obj, "size"));
    if (sz > data_size - data_pos)
        sz = data_size - data_pos;
    return rb_funcall(cMmapScanner, rb_intern("new"), 3, obj, SIZET2NUM(data_pos), SIZET2NUM(sz));
}

static VALUE eos_p(VALUE obj)
{
    size_t data_pos = NUM2SIZET(rb_iv_get(obj, "pos"));
    size_t data_size = NUM2SIZET(rb_iv_get(obj, "size"));
    return data_pos >= data_size ? Qtrue : Qfalse;
}

static VALUE rest(VALUE obj)
{
    return rb_funcall(cMmapScanner, rb_intern("new"), 2, obj, rb_iv_get(obj, "pos"));
}

static VALUE matched(VALUE obj)
{
    VALUE pos = rb_iv_get(obj, "matched_pos");
    if (pos == Qnil)
        return Qnil;
    return rb_funcall(cMmapScanner, rb_intern("new"), 3, obj, pos, rb_iv_get(obj, "matched_len"));
}

void Init_mmapscanner(void)
{
    cMmapScanner = rb_define_class("MmapScanner", rb_cObject);
    rb_define_method(cMmapScanner, "initialize", initialize, -1);
    rb_define_method(cMmapScanner, "size", size, 0);
    rb_define_method(cMmapScanner, "length", size, 0);
    rb_define_method(cMmapScanner, "to_s", to_s, 0);
    rb_define_method(cMmapScanner, "slice", slice, 2);
//    rb_define_method(cMmapScanner, "[]", slice, 2);
    rb_define_method(cMmapScanner, "inspect", inspect, 0);
    rb_define_method(cMmapScanner, "pos", pos, 0);
    rb_define_method(cMmapScanner, "pos=", set_pos, 1);
    rb_define_method(cMmapScanner, "scan", scan, 1);
    rb_define_method(cMmapScanner, "scan_until", scan_until, 1);
    rb_define_method(cMmapScanner, "check", check, 1);
    rb_define_method(cMmapScanner, "skip", skip, 1);
    rb_define_method(cMmapScanner, "match?", match_p, 1);
    rb_define_method(cMmapScanner, "peek", peek, 1);
    rb_define_method(cMmapScanner, "eos?", eos_p, 0);
    rb_define_method(cMmapScanner, "rest", rest, 0);
    rb_define_method(cMmapScanner, "matched", matched, 0);

    cMmap = rb_define_class_under(cMmapScanner, "Mmap", rb_cObject);
}
