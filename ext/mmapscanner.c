#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <ruby.h>
#ifdef HAVE_RUBY_RUBY_H
#include <ruby/io.h>
#include <ruby/regex.h>
#else
#include <re.h>
#endif

#ifndef NUM2SIZET
#define NUM2SIZET NUM2ULONG
#define SIZET2NUM ULONG2NUM
#endif

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
    xfree(data);
}

static VALUE mmap_allocate(VALUE klass)
{
    mmap_data_t *data;
    data = xmalloc(sizeof(mmap_data_t));
    data->ptr = NULL;
    data->size = 0;
    return Data_Wrap_Struct(klass, 0, mmap_free, data);
}

static VALUE mmap_initialize(int argc, VALUE *argv, VALUE obj)
{
    mmap_data_t *data;
    Data_Get_Struct(obj, mmap_data_t, data);
    if (data->ptr)
        rb_raise(rb_eRuntimeError, "already mapped");
    VALUE file, voffset, vlength;
    off_t offset = 0;
    size_t length = 0;
    rb_scan_args(argc, argv, "12", &file, &voffset, &vlength);
    if (TYPE(file) != T_FILE)
        rb_raise(rb_eTypeError, "File object required");
    if (voffset != Qnil && NUM2LL(voffset) < 0)
        rb_raise(rb_eRangeError, "offset out of range: %lld", NUM2LL(voffset));
    if (vlength != Qnil && NUM2LL(vlength) < 0)
        rb_raise(rb_eRangeError, "length out of range: %lld", NUM2LL(vlength));
    int fd = FIX2INT(rb_funcall(file, rb_intern("fileno"), 0));
    struct stat st;
    if (fstat(fd, &st) < 0)
        rb_sys_fail("fstat");
    offset = voffset == Qnil ? 0 : NUM2SIZET(voffset);
    length = vlength == Qnil ? st.st_size : NUM2SIZET(vlength);
    if (offset + length > st.st_size)
        length = st.st_size - offset;
    void *ptr;
    if ((ptr = mmap(NULL, length, PROT_READ, MAP_SHARED, fd, offset)) == MAP_FAILED)
        rb_sys_fail("mmap");

    data->ptr = ptr;
    data->size = length;
    return Qnil;
}

static VALUE mmap_size(VALUE obj)
{
    mmap_data_t *data;
    Data_Get_Struct(obj, mmap_data_t, data);
    return SIZET2NUM(data->size);
}

static VALUE mmap_unmap(VALUE obj)
{
    mmap_data_t *data;
    Data_Get_Struct(obj, mmap_data_t, data);
    if (data->ptr == NULL)
        rb_raise(rb_eRuntimeError, "already unmapped");
    if (munmap(data->ptr, data->size) < 0)
        rb_sys_fail("munmap");
    data->ptr = NULL;
    return Qnil;
}

static void mmapscanner_free(mmapscanner_t *ms)
{
#ifdef HAVE_RUBY_ONIGURUMA_H
    onig_region_free(&ms->regs, 0);
#else
    re_free_registers(&ms->regs);
#endif
    xfree(ms);
}

static void mark(mmapscanner_t *ms)
{
    rb_gc_mark_maybe(ms->data);
    rb_gc_mark_maybe(ms->dummy_str);
}

VALUE allocate(VALUE klass)
{
    mmapscanner_t *ms;
    ms = xmalloc(sizeof *ms);
    ms->offset = 0;
    ms->size = 0;
    ms->pos = 0;
    ms->matched = 0;
    ms->matched_pos = 0;
#ifdef HAVE_RUBY_ONIGURUMA_H
    onig_region_init(&ms->regs);
#else
    memset(&ms->regs, 0, sizeof ms->regs);
#endif
    ms->data = Qnil;
    ms->dummy_str = Qnil;
    return Data_Wrap_Struct(klass, mark, mmapscanner_free, ms);
}

static VALUE initialize(int argc, VALUE *argv, VALUE obj)
{
    VALUE src, voffset, vsize;
    size_t offset, size;
    size_t src_offset = 0, src_size = 0;
    mmapscanner_t *self;
    int src_size_defined = 0;

    rb_scan_args(argc, argv, "12", &src, &voffset, &vsize);
    if (voffset != Qnil && NUM2LL(voffset) < 0)
        rb_raise(rb_eRangeError, "offset out of range: %lld", NUM2LL(voffset));
    if (vsize != Qnil && NUM2LL(vsize) < 0)
        rb_raise(rb_eRangeError, "length out of range: %lld", NUM2LL(vsize));
    offset = voffset == Qnil ? 0 : NUM2SIZET(voffset);
    if (rb_obj_class(src) == cMmapScanner) {
        mmapscanner_t *ms;
        Data_Get_Struct(src, mmapscanner_t, ms);
        src_offset = ms->offset;
        src_size = ms->size;
        src = ms->data;
        src_size_defined = 1;
    } else if (TYPE(src) == T_FILE) {
        src = rb_funcall(cMmap, rb_intern("new"), 1, src);
    }
    if (rb_obj_class(src) == cMmap) {
        if (!src_size_defined) {
            mmap_data_t *data;
            Data_Get_Struct(src, mmap_data_t, data);
            src_size = data->size;
        }
    } else if (TYPE(src) == T_STRING) {
        if (!src_size_defined)
            src_size = RSTRING_LEN(src);
    } else {
        rb_raise(rb_eTypeError, "wrong argument type %s (expected File/String/MmapScanner/MmapScanner::Mmap)", rb_obj_classname(src));
    }
    if (offset > src_size)
        rb_raise(rb_eRangeError, "length out of range: %zu > %zu", offset, src_size);
    size = vsize == Qnil ? src_size - offset : NUM2SIZET(vsize);
    if (size > src_size - offset)
        size = src_size - offset;

    Data_Get_Struct(obj, mmapscanner_t, self);
    self->offset = src_offset + offset;
    self->size = size;
    self->pos = 0;
    self->matched = 0;
    self->matched_pos = 0;
    self->data = src;
    return Qnil;
}

static VALUE create_from_mmapscanner(VALUE src, size_t offset, size_t size)
{
    mmapscanner_t *ms, *new;
    VALUE obj;

    Data_Get_Struct(src, mmapscanner_t, ms);

    if (offset > ms->size)
        rb_raise(rb_eRangeError, "length out of range: %zu > %zu", offset, ms->size);
    if (size > ms->size - offset)
        size = ms->size - offset;

    obj = allocate(cMmapScanner);
    Data_Get_Struct(obj, mmapscanner_t, new);
    new->offset = ms->offset + offset;
    new->size = size;
    new->data = ms->data;
    return obj;
}

static VALUE size(VALUE obj)
{
    mmapscanner_t *ms;
    Data_Get_Struct(obj, mmapscanner_t, ms);
    return SIZET2NUM(ms->size);
}

static VALUE data(VALUE obj)
{
    mmapscanner_t *ms;
    Data_Get_Struct(obj, mmapscanner_t, ms);
    return ms->data;
}

static VALUE to_s(VALUE obj)
{
    mmapscanner_t *ms;
    mmap_data_t *mdata;
    Data_Get_Struct(obj, mmapscanner_t, ms);
    if (TYPE(ms->data) == T_STRING)
        return rb_str_new(RSTRING_PTR(ms->data) + ms->offset, ms->size);
    Data_Get_Struct(ms->data, mmap_data_t, mdata);
    if (mdata->ptr == NULL)
        rb_raise(rb_eRuntimeError, "already unmapped");
    return rb_str_new(mdata->ptr + ms->offset, ms->size);
}

static VALUE slice(VALUE obj, VALUE pos, VALUE len)
{
    if (NUM2LL(pos) < 0)
        rb_raise(rb_eRangeError, "offset out of range: %lld", NUM2LL(pos));
    if (NUM2LL(len) < 0)
        rb_raise(rb_eRangeError, "length out of range: %lld", NUM2LL(len));
    return create_from_mmapscanner(obj, NUM2ULL(pos), NUM2ULL(len));
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
    mmap_data_t *mdata;

    ms->matched = 0;
    Check_Type(re, T_REGEXP);
   if (ms->pos > ms->size)
       return Qnil;
    if (TYPE(ms->data) == T_STRING)
        ptr = RSTRING_PTR(ms->data);
    else {
        Data_Get_Struct(ms->data, mmap_data_t, mdata);
        if (mdata->ptr == NULL)
            rb_raise(rb_eRuntimeError, "already unmapped");
        ptr = mdata->ptr;
    }
    ptr += ms->offset;

#ifdef HAVE_RUBY_ONIGURUMA_H
    if (ms->dummy_str == Qnil)
        ms->dummy_str = rb_str_new("", 0);
    reg = rb_reg_prepare_re(re, ms->dummy_str);
    tmpreg = reg != RREGEXP(re)->ptr;
    if (!tmpreg) RREGEXP(re)->usecnt++;

    if (headonly) {
        result = onig_match(reg, (UChar*)(ptr + ms->pos),
                            (UChar*)(ptr + ms->size),
                            (UChar*)(ptr + ms->pos),
                            &ms->regs, ONIG_OPTION_NONE);
    } else {
        result = onig_search(reg, (UChar*)(ptr + ms->pos),
                             (UChar*)(ptr + ms->size),
                             (UChar*)(ptr + ms->pos),
                             (UChar*)(ptr + ms->size),
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
#else
    if (headonly) {
        result = re_match(RREGEXP(re)->ptr,
                          ptr+ms->pos, ms->size - ms->pos,
                          0,
                          &(ms->regs));
    } else {
        result = re_search(RREGEXP(re)->ptr,
                           ptr+ms->pos, ms->size - ms->pos,
                           0,
                           ms->size,
                           &(ms->regs));
    }
#endif
    if (result < 0)
        return Qnil;
    old_pos = ms->pos;
    matched_len = ms->regs.end[0];
    if (forward)
        ms->pos += matched_len;
    ms->matched = 1;
    ms->matched_pos = old_pos;

    if (sizeonly)
        return SIZET2NUM(matched_len);
    return create_from_mmapscanner(obj, old_pos, matched_len);
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
    return create_from_mmapscanner(obj, ms->pos, sz);
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
    return create_from_mmapscanner(obj, ms->pos, ms->size - ms->pos);
}

static int matched_sub(int argc, VALUE *argv, mmapscanner_t *ms, size_t *pos, size_t *len)
{
    int i = 0;
    if (ms->matched == 0)
        return 0;
    if (argc == 0)
        i = 0;
    else if (argc == 1)
        i = NUM2LONG(argv[0]);
    else
        rb_raise(rb_eArgError, "wrong number of arguments (%d for 0..1)", argc);
    if (i < 0)
        return 0;
    if (i >= ms->regs.num_regs)
        return 0;
    if (ms->regs.beg[i] < 0 || ms->regs.end[i] < 0)
        return 0;
    *pos = ms->matched_pos + ms->regs.beg[i];
    *len = ms->regs.end[i] - ms->regs.beg[i];
    return 1;
}

static VALUE matched(int argc, VALUE *argv, VALUE obj)
{
    mmapscanner_t *ms;
    Data_Get_Struct(obj, mmapscanner_t, ms);
    size_t pos, len;
    if (matched_sub(argc, argv, ms, &pos, &len) == 0)
        return Qnil;
    return create_from_mmapscanner(obj, pos, len);
}

static VALUE matched_str(int argc, VALUE *argv, VALUE obj)
{
    mmapscanner_t *ms;
    Data_Get_Struct(obj, mmapscanner_t, ms);
    mmap_data_t *mdata;
    size_t pos, len;
    if (matched_sub(argc, argv, ms, &pos, &len) == 0)
        return Qnil;
    if (TYPE(ms->data) == T_STRING)
        return rb_str_new(RSTRING_PTR(ms->data)+ms->offset+pos, len);
    Data_Get_Struct(ms->data, mmap_data_t, mdata);
    if (mdata->ptr == NULL)
        rb_raise(rb_eRuntimeError, "already unmapped");
    return rb_str_new(mdata->ptr+ms->offset+pos, len);
}

void Init_mmapscanner(void)
{
    cMmapScanner = rb_define_class("MmapScanner", rb_cObject);
    rb_define_alloc_func(cMmapScanner, allocate);
    rb_define_method(cMmapScanner, "initialize", initialize, -1);
    rb_define_method(cMmapScanner, "size", size, 0);
    rb_define_method(cMmapScanner, "length", size, 0);
    rb_define_method(cMmapScanner, "data", data, 0);
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
    rb_define_method(cMmapScanner, "matched_str", matched_str, -1);

    cMmap = rb_define_class_under(cMmapScanner, "Mmap", rb_cObject);
    rb_define_alloc_func(cMmap, mmap_allocate);
    rb_define_method(cMmap, "initialize", mmap_initialize, -1);
    rb_define_method(cMmap, "size", mmap_size, 0);
    rb_define_method(cMmap, "unmap", mmap_unmap, 0);
}
