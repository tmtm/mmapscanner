#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <ruby.h>
#include <ruby/io.h>

static VALUE cMmapScanner;

typedef struct {
    char *ptr;
    size_t size;
    size_t pos;
} mmap_data_t;

static void mmap_free(mmap_data_t *data)
{
    if (data->ptr)
        munmap(data->ptr, data->size);
    free(data);
}

static VALUE allocate(VALUE klass)
{
    VALUE obj;
    mmap_data_t *data;

    data = xmalloc(sizeof(mmap_data_t));
    data->ptr = NULL;
    data->size = 0;
    data->pos = 0;
    obj = Data_Wrap_Struct(klass, 0, mmap_free, data);
    rb_iv_set(obj, "parent", Qnil);
    return obj;
}

static VALUE initialize(int argc, VALUE *argv, VALUE obj)
{
    VALUE src, size, pos, p, pp;
    int fd;
    void *ptr;
    mmap_data_t *data, *parent;
    struct stat st;
    size_t sz, offset;

    Data_Get_Struct(obj, mmap_data_t, data);
    if (data->ptr)
        rb_raise(rb_eRuntimeError, "already initialized");
    rb_scan_args(argc, argv, "12", &src, &pos, &size);
    if (pos != Qnil && NUM2LL(pos) < 0)
        rb_raise(rb_eRangeError, "position out of range: %lld", NUM2LL(pos));
    if (size != Qnil && NUM2LL(size) < 0)
        rb_raise(rb_eRangeError, "length out of range: %lld", NUM2LL(size));
    offset = pos == Qnil ? 0 : NUM2SIZET(pos);
    if (rb_obj_class(src) == cMmapScanner) {
        Data_Get_Struct(src, mmap_data_t, parent);
        if (offset >= parent->size)
            rb_raise(rb_eRangeError, "length out of range: %zu >= %zu", offset, parent->size);
        sz = size == Qnil ? parent->size - offset : NUM2SIZET(size);
        ptr = parent->ptr + offset;
        if (sz > parent->size - offset)
            sz = parent->size-offset;
        data->ptr = ptr;
        data->size = sz;
        p = src;
        while ((pp = rb_iv_get(p, "parent")) != Qnil)
            p = pp;
        rb_iv_set(obj, "parent", p);
        return;
    }
    Check_Type(src, T_FILE);
    fd = RFILE(src)->fptr->fd;
    fstat(fd, &st);
    sz = size == Qnil ? st.st_size - offset : NUM2SIZET(size);
    if (sz > st.st_size - offset)
        sz = st.st_size - offset;
    if ((ptr = mmap(NULL, sz, PROT_READ, MAP_SHARED, fd, offset)) == MAP_FAILED) {
        rb_exc_raise(rb_funcall(rb_eSystemCallError, rb_intern("new"), 1, INT2FIX(errno)));
    }
    data->ptr = ptr;
    data->size = sz;
}

static VALUE size(VALUE obj)
{
    mmap_data_t *data;

    Data_Get_Struct(obj, mmap_data_t, data);
    return ULL2NUM(data->size);
}

static VALUE to_s(VALUE obj)
{
    mmap_data_t *data;

    Data_Get_Struct(obj, mmap_data_t, data);
    return rb_str_new(data->ptr, data->size);
}

static VALUE slice(VALUE obj, VALUE pos, VALUE len)
{
    size_t offset;
    size_t length;
    mmap_data_t *data;

    Data_Get_Struct(obj, mmap_data_t, data);
    return rb_funcall(cMmapScanner, rb_intern("new"), 3, obj, pos, len);
}

static VALUE inspect(VALUE obj)
{
    rb_str_new2("#<MmapScanner>");
}

static VALUE pos(VALUE obj)
{
    mmap_data_t *data;

    Data_Get_Struct(obj, mmap_data_t, data);
    return ULL2NUM(data->pos);
}

static VALUE set_pos(VALUE obj, VALUE pos)
{
    mmap_data_t *data;
    size_t p;

    if (NUM2LL(pos) < 0)
        rb_raise(rb_eRangeError, "out of range: %lld", NUM2LL(pos));
    Data_Get_Struct(obj, mmap_data_t, data);
    p = NUM2SIZET(pos);
    if (p > data->size)
        rb_raise(rb_eRangeError, "out of range: %zu > %zu", p, data->size);
    data->pos = p;
    return pos;
}

static VALUE scan_sub(VALUE obj, VALUE re, int forward)
{
    regex_t *rb_reg_prepare_re(VALUE re, VALUE str);
    mmap_data_t *data;
    regex_t *reg;
    int tmpreg;
    int result;
    struct re_registers regs;
    size_t old_pos, matched_len;

    Check_Type(re, T_REGEXP);
    Data_Get_Struct(obj, mmap_data_t, data);
    if (data->pos >= data->size)
        return Qnil;

    reg = rb_reg_prepare_re(re, rb_str_new("", 0));
    tmpreg = reg != RREGEXP(re)->ptr;
    if (!tmpreg) RREGEXP(re)->usecnt++;

    onig_region_init(&regs);
    result = onig_match(reg, (UChar* )(data->ptr+data->pos),
                        (UChar* )(data->ptr+data->size),
                        (UChar* )(data->ptr+data->pos),
                        &regs, ONIG_OPTION_NONE);
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
    old_pos = data->pos;
    matched_len = regs.end[0];
    if (forward)
        data->pos += matched_len;
    return rb_funcall(cMmapScanner, rb_intern("new"), 3, obj, ULL2NUM(old_pos), ULL2NUM(matched_len));
}

static VALUE scan(VALUE obj, VALUE re)
{
    return scan_sub(obj, re, 1);
}

static VALUE check(VALUE obj, VALUE re)
{
    return scan_sub(obj, re, 0);
}

static VALUE skip(VALUE obj, VALUE re)
{
    mmap_data_t *data;
    VALUE ret = scan_sub(obj, re, 1);
    if (ret == Qnil)
        return ret;
    Data_Get_Struct(ret, mmap_data_t, data);
    return ULL2NUM(data->size);
}

static VALUE match_p(VALUE obj, VALUE re)
{
    mmap_data_t *data;
    VALUE ret = scan_sub(obj, re, 0);
    if (ret == Qnil)
        return ret;
    Data_Get_Struct(ret, mmap_data_t, data);
    return ULL2NUM(data->size);
}

static VALUE peek(VALUE obj, VALUE size)
{
    size_t sz = NUM2SIZET(size);
    mmap_data_t *data;
    Data_Get_Struct(obj, mmap_data_t, data);
    if (sz > data->size - data->pos)
        sz = data->size - data->pos;
    return rb_funcall(cMmapScanner, rb_intern("new"), 3, obj, SIZET2NUM(data->pos), SIZET2NUM(sz));
}

static VALUE eos_p(VALUE obj)
{
    mmap_data_t *data;
    Data_Get_Struct(obj, mmap_data_t, data);
    return data->pos >= data->size ? Qtrue : Qfalse;
}

static VALUE rest(VALUE obj)
{
    mmap_data_t *data;
    Data_Get_Struct(obj, mmap_data_t, data);
    return rb_funcall(cMmapScanner, rb_intern("new"), 2, obj, SIZET2NUM(data->pos));
}

void Init_mmapscanner(void)
{
    cMmapScanner = rb_define_class("MmapScanner", rb_cObject);
    rb_define_alloc_func(cMmapScanner, allocate);
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
    rb_define_method(cMmapScanner, "check", check, 1);
    rb_define_method(cMmapScanner, "skip", skip, 1);
    rb_define_method(cMmapScanner, "match?", match_p, 1);
    rb_define_method(cMmapScanner, "peek", peek, 1);
    rb_define_method(cMmapScanner, "eos?", eos_p, 0);
    rb_define_method(cMmapScanner, "rest", rest, 0);
}
