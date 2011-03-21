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

typedef struct {
    size_t offset;
    size_t size;
    size_t pos;
    int matched;
    size_t matched_pos;
    struct re_registers regs;
    VALUE data;
    VALUE dummy_str;
} mmapscanner_t;

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

static void mmapscanner_free(mmapscanner_t *ms)
{
    onig_region_free(&ms->regs, 0);
    free(ms);
}

static void mark(mmapscanner_t *ms)
{
    rb_gc_mark_maybe(ms->data);
    rb_gc_mark_maybe(ms->dummy_str);
}

VALUE allocate(VALUE klass)
{
    mmapscanner_t *ms;
    ms = malloc(sizeof *ms);
    ms->offset = 0;
    ms->size = 0;
    ms->pos = 0;
    ms->matched_pos = 0;
    onig_region_init(&ms->regs);
    ms->dummy_str = rb_str_new("", 0);
    return Data_Wrap_Struct(klass, mark, mmapscanner_free, ms);
}

static VALUE initialize(int argc, VALUE *argv, VALUE obj)
{
    VALUE src, voffset, vsize;
    size_t offset, size;
    size_t src_offset, src_size;
    VALUE src_data;
    mmapscanner_t *ms;

    rb_scan_args(argc, argv, "12", &src, &voffset, &vsize);
    if (voffset != Qnil && NUM2LL(voffset) < 0)
        rb_raise(rb_eRangeError, "offset out of range: %lld", NUM2LL(voffset));
    if (vsize != Qnil && NUM2LL(vsize) < 0)
        rb_raise(rb_eRangeError, "length out of range: %lld", NUM2LL(vsize));
    offset = voffset == Qnil ? 0 : NUM2SIZET(voffset);
    if (rb_obj_class(src) == cMmapScanner) {
        Data_Get_Struct(src, mmapscanner_t, ms);
        src_offset = ms->offset;
        src_size = ms->size;
        src_data = ms->data;
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
    if (offset > src_size)
        rb_raise(rb_eRangeError, "length out of range: %zu > %zu", offset, src_size);
    size = vsize == Qnil ? src_size - offset : NUM2SIZET(vsize);
    if (size > src_size - offset)
        size = src_size - offset;

    Data_Get_Struct(obj, mmapscanner_t, ms);
    ms->offset = src_offset + offset;
    ms->size = size;
    ms->pos = 0;
    ms->matched = 0;
    ms->matched_pos = 0;
    ms->data = src_data;
    return Qnil;
}

static VALUE size(VALUE obj)
{
    mmapscanner_t *ms;
    Data_Get_Struct(obj, mmapscanner_t, ms);
    return SIZET2NUM(ms->size);
}

static VALUE to_s(VALUE obj)
{
    mmapscanner_t *ms;
    Data_Get_Struct(obj, mmapscanner_t, ms);
    size_t offset = ms->offset;
    size_t size = ms->size;
    VALUE data = ms->data;
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
    mmapscanner_t *ms;
    Data_Get_Struct(obj, mmapscanner_t, ms);
    return SIZET2NUM(ms->pos);
}

static VALUE set_pos(VALUE obj, VALUE pos)
{
    mmapscanner_t *ms;
    Data_Get_Struct(obj, mmapscanner_t, ms);
    size_t p, size;

    if (NUM2LL(pos) < 0)
        rb_raise(rb_eRangeError, "out of range: %lld", NUM2LL(pos));
    p = NUM2SIZET(pos);
    size = ms->size;
    if (p > size)
        rb_raise(rb_eRangeError, "out of range: %zu > %zu", p, size);
    ms->pos = p;
    return pos;
}

static VALUE scan_sub(VALUE obj, VALUE re, int forward, int headonly, int sizeonly)
{
    mmapscanner_t *ms;
    Data_Get_Struct(obj, mmapscanner_t, ms);
    regex_t *rb_reg_prepare_re(VALUE re, VALUE str);
    regex_t *reg;
    int tmpreg;
    int result;
    size_t old_pos, matched_len;
    char *ptr;
    size_t pos, size;
    VALUE data;
    mmap_data_t *mdata;

    Check_Type(re, T_REGEXP);
    pos = ms->pos;
    size = ms->size;
    if (pos >= size)
        return Qnil;
    data = ms->data;
    if (TYPE(data) == T_STRING)
        ptr = RSTRING_PTR(data);
    else {
        Data_Get_Struct(data, mmap_data_t, mdata);
        ptr = mdata->ptr;
    }
    ptr += ms->offset;

    reg = rb_reg_prepare_re(re, ms->dummy_str);
    tmpreg = reg != RREGEXP(re)->ptr;
    if (!tmpreg) RREGEXP(re)->usecnt++;

    if (headonly) {
        result = onig_match(reg, (UChar*)(ptr+pos),
                            (UChar*)(ptr+size),
                            (UChar*)(ptr+pos),
                            &ms->regs, ONIG_OPTION_NONE);
    } else {
        result = onig_search(reg, (UChar*)(ptr+pos),
                             (UChar*)(ptr+size),
                             (UChar*)(ptr+pos),
                             (UChar*)(ptr+size),
                             &ms->regs, ONIG_OPTION_NONE);
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
    matched_len = ms->regs.end[0];
    if (forward) {
        pos += matched_len;
        ms->pos = pos;
    }
    ms->matched = 1;
    ms->matched_pos = old_pos;

    if (sizeonly)
        return SIZET2NUM(matched_len);
    return rb_funcall(cMmapScanner, rb_intern("new"), 3, obj, SIZET2NUM(old_pos), SIZET2NUM(matched_len));
}

static VALUE scan(VALUE obj, VALUE re)
{
    return scan_sub(obj, re, 1, 1, 0);
}

static VALUE scan_until(VALUE obj, VALUE re)
{
    return scan_sub(obj, re, 1, 0, 0);
}

static VALUE check(VALUE obj, VALUE re)
{
    return scan_sub(obj, re, 0, 1, 0);
}

static VALUE skip(VALUE obj, VALUE re)
{
    return scan_sub(obj, re, 1, 1, 1);
}

static VALUE match_p(VALUE obj, VALUE re)
{
    return scan_sub(obj, re, 0, 1, 1);
}

static VALUE peek(VALUE obj, VALUE size)
{
    mmapscanner_t *ms;
    Data_Get_Struct(obj, mmapscanner_t, ms);
    size_t sz = NUM2SIZET(size);
    if (sz > ms->size - ms->pos)
        sz = ms->size - ms->pos;
    return rb_funcall(cMmapScanner, rb_intern("new"), 3, obj, SIZET2NUM(ms->pos), SIZET2NUM(sz));
}

static VALUE eos_p(VALUE obj)
{
    mmapscanner_t *ms;
    Data_Get_Struct(obj, mmapscanner_t, ms);
    return ms->pos >= ms->size ? Qtrue : Qfalse;
}

static VALUE rest(VALUE obj)
{
    mmapscanner_t *ms;
    Data_Get_Struct(obj, mmapscanner_t, ms);
    return rb_funcall(cMmapScanner, rb_intern("new"), 2, obj, SIZET2NUM(ms->pos));
}

static VALUE matched(int argc, VALUE *argv, VALUE obj)
{
    mmapscanner_t *ms;
    Data_Get_Struct(obj, mmapscanner_t, ms);
    VALUE nth;
    int i = 0;
    size_t pos, len;

    if (rb_scan_args(argc, argv, "01", &nth) == 1)
        i = NUM2LONG(nth);
    if (ms->matched == 0)
        return Qnil;
    if (i < 0)
        return Qnil;
    if (i >= ms->regs.num_regs)
        return Qnil;
    pos = ms->matched_pos + ms->regs.beg[i];
    len = ms->regs.end[i] - ms->regs.beg[i];
    return rb_funcall(cMmapScanner, rb_intern("new"), 3, obj, SIZET2NUM(pos), SIZET2NUM(len));
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
    rb_define_method(cMmapScanner, "scan_until", scan_until, 1);
    rb_define_method(cMmapScanner, "check", check, 1);
    rb_define_method(cMmapScanner, "skip", skip, 1);
    rb_define_method(cMmapScanner, "match?", match_p, 1);
    rb_define_method(cMmapScanner, "peek", peek, 1);
    rb_define_method(cMmapScanner, "eos?", eos_p, 0);
    rb_define_method(cMmapScanner, "rest", rest, 0);
    rb_define_method(cMmapScanner, "matched", matched, -1);

    cMmap = rb_define_class_under(cMmapScanner, "Mmap", rb_cObject);
}
