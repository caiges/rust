// Functions that interpret the shape of a type to perform various low-level
// actions, such as copying, freeing, comparing, and so on.

#ifndef RUST_SHAPE_H
#define RUST_SHAPE_H

#include "rust_internal.h"

#ifdef _MSC_VER
#define ALIGNOF     __alignof
#else
#define ALIGNOF     __alignof__
#endif

#define ARENA_SIZE          256

#define DPRINT(fmt,...)     fprintf(stderr, fmt, ##__VA_ARGS__)
#define DPRINTCX(cx)        print::print_cx(cx)

//#define DPRINT(fmt,...)
//#define DPRINTCX(cx)


namespace shape {


// Constants

const uint8_t SHAPE_U8 = 0u;
const uint8_t SHAPE_U16 = 1u;
const uint8_t SHAPE_U32 = 2u;
const uint8_t SHAPE_U64 = 3u;
const uint8_t SHAPE_I8 = 4u;
const uint8_t SHAPE_I16 = 5u;
const uint8_t SHAPE_I32 = 6u;
const uint8_t SHAPE_I64 = 7u;
const uint8_t SHAPE_F32 = 8u;
const uint8_t SHAPE_F64 = 9u;
const uint8_t SHAPE_EVEC = 10u;
const uint8_t SHAPE_IVEC = 11u;
const uint8_t SHAPE_TAG = 12u;
const uint8_t SHAPE_BOX = 13u;
const uint8_t SHAPE_STRUCT = 17u;
const uint8_t SHAPE_FN = 18u;
const uint8_t SHAPE_OBJ = 19u;
const uint8_t SHAPE_RES = 20u;
const uint8_t SHAPE_VAR = 21u;


// Forward declarations

struct rust_obj;
struct size_align;
struct type_param;


// Arenas; these functions must execute very quickly, so we use an arena
// instead of malloc or new.

class arena {
    uint8_t *ptr;
    uint8_t data[ARENA_SIZE];

public:
    arena() : ptr(data) {}

    template<typename T>
    inline T *alloc(size_t count = 1) {
        // FIXME: align
        size_t sz = count * sizeof(T);
        T *rv = (T *)ptr;
        ptr += sz;
        if (ptr > &data[ARENA_SIZE]) {
            fprintf(stderr, "Arena space exhausted, sorry\n");
            abort();
        }
        return rv;
    }
};


// Utility classes

struct size_align {
    size_t size;
    size_t alignment;

    size_align(size_t in_size = 0, size_t in_align = 1) :
        size(in_size), alignment(in_align) {}

    bool is_set() const { return alignment != 0; }

    inline void set(size_t in_size, size_t in_align) {
        size = in_size;
        alignment = in_align;
    }

    inline void add(const size_align &other) {
        add(other.size, other.alignment);
    }

    inline void add(size_t extra_size, size_t extra_align) {
        size += extra_size;
        alignment = max(alignment, extra_align);
    }

    static inline size_align make(size_t in_size) {
        size_align sa;
        sa.size = sa.alignment = in_size;
        return sa;
    }

    static inline size_align make(size_t in_size, size_t in_align) {
        size_align sa;
        sa.size = in_size;
        sa.alignment = in_align;
        return sa;
    }
};

struct tag_info {
    uint16_t tag_id;                        // The tag ID.
    const uint8_t *info_ptr;                // Pointer to the info table.
    uint16_t variant_count;                 // Number of variants in the tag.
    const uint8_t *largest_variants_ptr;    // Ptr to largest variants table.
    size_align tag_sa;                      // Size and align of this tag.
    uint16_t n_params;                      // Number of type parameters.
    const type_param *params;               // Array of type parameters.
};


// Contexts

// The base context, an abstract class. We use the curiously recurring
// template pattern here to avoid virtual dispatch.
template<typename T>
class ctxt {
public:
    const uint8_t *sp;                  // shape pointer
    const type_param *params;           // shapes of type parameters
    const rust_shape_tables *tables;
    rust_task *task;

    ctxt(rust_task *in_task,
         const uint8_t *in_sp,
         const type_param *in_params,
         const rust_shape_tables *in_tables)
    : sp(in_sp), params(in_params), tables(in_tables), task(in_task) {}

    template<typename U>
    ctxt(const ctxt<U> &other,
         const uint8_t *in_sp = NULL,
         const type_param *in_params = NULL,
         const rust_shape_tables *in_tables = NULL)
    : sp(in_sp ? in_sp : other.sp),
      params(in_params ? in_params : other.params),
      tables(in_tables ? in_tables : other.tables),
      task(other.task) {}

    void walk(bool align);
    void walk_reset(bool align);

    std::pair<const uint8_t *,const uint8_t *>
    get_variant_sp(tag_info &info, uint32_t variant_id);

protected:
    inline uint8_t peek() { return *sp; }

    static inline uint16_t get_u16(const uint8_t *addr);
    static inline uint16_t get_u16_bump(const uint8_t *&addr);
    inline size_align get_size_align(const uint8_t *&addr);

private:
    void walk_evec(bool align);
    void walk_ivec(bool align);
    void walk_tag(bool align);
    void walk_box(bool align);
    void walk_struct(bool align);
    void walk_res(bool align);
    void walk_var(bool align);
};


// Core Rust types

struct rust_fn {
    void (*code)(uint8_t *rv, rust_task *task, void *env, ...);
    void *env;
};

struct rust_closure {
    type_desc *tydesc;
    uint32_t target_0;
    uint32_t target_1;
    uint32_t bindings[0];

    uint8_t *get_bindings() const { return (uint8_t *)bindings; }
};

struct rust_obj_box {
    type_desc *tydesc;

    uint8_t *get_bindings() const { return (uint8_t *)this; }
};

struct rust_vtable {
    CDECL void (*dtor)(void *rv, rust_task *task, rust_obj obj);
};

struct rust_obj {
    rust_vtable *vtable;
    void *box;
};


// Type parameters

struct type_param {
    const uint8_t *shape;
    const rust_shape_tables *tables;
    const struct type_param *params;    // subparameters

    template<typename T>
    inline void set(ctxt<T> *cx) {
        shape = cx->sp;
        tables = cx->tables;
        params = cx->params;
    }

    static type_param *make(const type_desc *tydesc, arena &arena) {
        uint32_t n_params = tydesc->n_params;
        if (!n_params)
            return NULL;

        type_param *ptrs = arena.alloc<type_param>(n_params);
        for (uint32_t i = 0; i < n_params; i++) {
            const type_desc *subtydesc = tydesc->first_param[i];
            ptrs[i].shape = subtydesc->shape;
            ptrs[i].tables = subtydesc->shape_tables;
            ptrs[i].params = make(subtydesc, arena);
        }
        return ptrs;
    }
};


// Traversals

#define WALK_NUMBER(c_type) \
    static_cast<T *>(this)->template walk_number<c_type>(align)
#define WALK_SIMPLE(method) static_cast<T *>(this)->method(align)

template<typename T>
void
ctxt<T>::walk(bool align) {
    switch (*sp++) {
    case SHAPE_U8:      WALK_NUMBER(uint8_t);   break;
    case SHAPE_U16:     WALK_NUMBER(uint16_t);  break;
    case SHAPE_U32:     WALK_NUMBER(uint32_t);  break;
    case SHAPE_U64:     WALK_NUMBER(uint64_t);  break;
    case SHAPE_I8:      WALK_NUMBER(int8_t);    break;
    case SHAPE_I16:     WALK_NUMBER(int16_t);   break;
    case SHAPE_I32:     WALK_NUMBER(int32_t);   break;
    case SHAPE_I64:     WALK_NUMBER(int64_t);   break;
    case SHAPE_F32:     WALK_NUMBER(float);     break;
    case SHAPE_F64:     WALK_NUMBER(double);    break;
    case SHAPE_EVEC:    walk_evec(align);       break;
    case SHAPE_IVEC:    walk_ivec(align);       break;
    case SHAPE_TAG:     walk_tag(align);        break;
    case SHAPE_BOX:     walk_box(align);        break;
    case SHAPE_STRUCT:  walk_struct(align);     break;
    case SHAPE_FN:      WALK_SIMPLE(walk_fn);   break;
    case SHAPE_OBJ:     WALK_SIMPLE(walk_obj);  break;
    case SHAPE_RES:     walk_res(align);        break;
    case SHAPE_VAR:     walk_var(align);        break;
    default:            abort();
    }
}

template<typename T>
void
ctxt<T>::walk_reset(bool align) {
    const uint8_t *old_sp = sp;
    walk(align);
    sp = old_sp;
}

template<typename T>
uint16_t
ctxt<T>::get_u16(const uint8_t *addr) {
    return *reinterpret_cast<const uint16_t *>(addr);
}

template<typename T>
uint16_t
ctxt<T>::get_u16_bump(const uint8_t *&addr) {
    uint16_t result = get_u16(addr);
    addr += sizeof(uint16_t);
    return result;
}

template<typename T>
size_align
ctxt<T>::get_size_align(const uint8_t *&addr) {
    size_align result;
    result.size = get_u16_bump(addr);
    result.alignment = *addr++;
    return result;
}

// Returns a pointer to the beginning and a pointer to the end of the shape of
// the tag variant with the given ID.
template<typename T>
std::pair<const uint8_t *,const uint8_t *>
ctxt<T>::get_variant_sp(tag_info &tinfo, uint32_t variant_id) {
    uint16_t variant_offset = get_u16(tinfo.info_ptr +
                                      variant_id * sizeof(uint16_t));
    const uint8_t *variant_ptr = tables->tags + variant_offset;
    uint16_t variant_len = get_u16_bump(variant_ptr);
    const uint8_t *variant_end = variant_ptr + variant_len;
    return std::make_pair(variant_ptr, variant_end);
}

template<typename T>
void
ctxt<T>::walk_evec(bool align) {
    bool is_pod = *sp++;

    uint16_t sp_size = get_u16_bump(sp);
    const uint8_t *end_sp = sp + sp_size;

    static_cast<T *>(this)->walk_evec(align, is_pod, sp_size);

    sp = end_sp;
}

template<typename T>
void
ctxt<T>::walk_ivec(bool align) {
    bool is_pod = *sp++;
    size_align elem_sa = get_size_align(sp);

    uint16_t sp_size = get_u16_bump(sp);
    const uint8_t *end_sp = sp + sp_size;

    // FIXME: Hack to work around our incorrect alignment in some cases.
    if (elem_sa.alignment == 8)
        elem_sa.alignment = 4;

    static_cast<T *>(this)->walk_ivec(align, is_pod, elem_sa);

    sp = end_sp;
}

template<typename T>
void
ctxt<T>::walk_tag(bool align) {
    tag_info tinfo;
    tinfo.tag_id = get_u16_bump(sp);

    // Determine the info pointer.
    uint16_t info_offset = get_u16(tables->tags +
                                   tinfo.tag_id * sizeof(uint16_t));
    tinfo.info_ptr = tables->tags + info_offset;

    tinfo.variant_count = get_u16_bump(tinfo.info_ptr);

    // Determine the largest-variants pointer.
    uint16_t largest_variants_offset = get_u16_bump(tinfo.info_ptr);
    tinfo.largest_variants_ptr = tables->tags + largest_variants_offset;

    // Determine the size and alignment.
    tinfo.tag_sa = get_size_align(tinfo.info_ptr);

    // Determine the number of parameters.
    tinfo.n_params = get_u16_bump(sp);

    // Read in the tag type parameters.
    type_param params[tinfo.n_params];
    for (uint16_t i = 0; i < tinfo.n_params; i++) {
        uint16_t len = get_u16_bump(sp);
        params[i].set(this);
        sp += len;
    }

    tinfo.params = params;

    // Call to the implementation.
    static_cast<T *>(this)->walk_tag(align, tinfo);
}

template<typename T>
void
ctxt<T>::walk_box(bool align) {
    uint16_t sp_size = get_u16_bump(sp);
    const uint8_t *end_sp = sp + sp_size;

    static_cast<T *>(this)->walk_box(align);

    sp = end_sp;
}

template<typename T>
void
ctxt<T>::walk_struct(bool align) {
    uint16_t sp_size = get_u16_bump(sp);
    const uint8_t *end_sp = sp + sp_size;

    static_cast<T *>(this)->walk_struct(align, end_sp);

    sp = end_sp;
}

template<typename T>
void
ctxt<T>::walk_res(bool align) {
    uint16_t dtor_offset = get_u16_bump(sp);
    const rust_fn **resources =
        reinterpret_cast<const rust_fn **>(tables->resources);
    const rust_fn *dtor = resources[dtor_offset];

    uint16_t n_ty_params = get_u16_bump(sp);

    uint16_t ty_params_size = get_u16_bump(sp);
    const uint8_t *ty_params_sp = sp;
    sp += ty_params_size;

    uint16_t sp_size = get_u16_bump(sp);
    const uint8_t *end_sp = sp + sp_size;

    static_cast<T *>(this)->walk_res(align, dtor, n_ty_params, ty_params_sp);

    sp = end_sp;
}

template<typename T>
void
ctxt<T>::walk_var(bool align) {
    uint8_t param = *sp++;
    static_cast<T *>(this)->walk_var(align, param);
}

// A shape printer, useful for debugging

class print : public ctxt<print> {
public:
    template<typename T>
    print(const ctxt<T> &other,
          const uint8_t *in_sp = NULL,
          const type_param *in_params = NULL,
          const rust_shape_tables *in_tables = NULL)
    : ctxt<print>(other, in_sp, in_params, in_tables) {}

    void walk_tag(bool align, tag_info &tinfo);
    void walk_struct(bool align, const uint8_t *end_sp);
    void walk_res(bool align, const rust_fn *dtor, uint16_t n_ty_params,
                  const uint8_t *ty_params_sp);
    void walk_var(bool align, uint8_t param);

    void walk_evec(bool align, bool is_pod, uint16_t sp_size) {
        DPRINT("evec<"); walk(align); DPRINT(">");
    }
    void walk_ivec(bool align, bool is_pod, size_align &elem_sa) {
        DPRINT("ivec<"); walk(align); DPRINT(">");
    }
    void walk_box(bool align) {
        DPRINT("box<"); walk(align); DPRINT(">");
    }

    void walk_port(bool align)                  { DPRINT("port"); }
    void walk_chan(bool align)                  { DPRINT("chan"); }
    void walk_task(bool align)                  { DPRINT("task"); }
    void walk_fn(bool align)                    { DPRINT("fn");   }
    void walk_obj(bool align)                   { DPRINT("obj");  }

    template<typename T>
    void walk_number(bool align) {}

    template<typename T>
    static void print_cx(const T *cx) {
        print self(*cx);
        self.walk(false);
    }
};

//
// Size-of (which also computes alignment). Be warned: this is an expensive
// operation.
//
// TODO: Maybe dynamic_size_of() should call into this somehow?
//

class size_of : public ctxt<size_of> {
private:
    size_align sa;

public:
    size_of(const size_of &other,
            const uint8_t *in_sp = NULL,
            const type_param *in_params = NULL,
            const rust_shape_tables *in_tables = NULL)
    : ctxt<size_of>(other, in_sp, in_params, in_tables) {}

    template<typename T>
    size_of(const ctxt<T> &other,
            const uint8_t *in_sp = NULL,
            const type_param *in_params = NULL,
            const rust_shape_tables *in_tables = NULL)
    : ctxt<size_of>(other, in_sp, in_params, in_tables) {}

    void walk_tag(bool align, tag_info &tinfo);
    void walk_struct(bool align, const uint8_t *end_sp);
    void walk_ivec(bool align, bool is_pod, size_align &elem_sa);

    void walk_box(bool align)   { sa.set(sizeof(void *),   sizeof(void *)); }
    void walk_port(bool align)  { sa.set(sizeof(void *),   sizeof(void *)); }
    void walk_chan(bool align)  { sa.set(sizeof(void *),   sizeof(void *)); }
    void walk_task(bool align)  { sa.set(sizeof(void *),   sizeof(void *)); }
    void walk_fn(bool align)    { sa.set(sizeof(void *)*2, sizeof(void *)); }
    void walk_obj(bool align)   { sa.set(sizeof(void *)*2, sizeof(void *)); }

    void walk_evec(bool align, bool is_pod, uint16_t sp_size) {
        sa.set(sizeof(void *), sizeof(void *));
    }

    void walk_var(bool align, uint8_t param_index) {
        const type_param *param = &params[param_index];
        size_of sub(*this, param->shape, param->params, param->tables);
        sub.walk(align);
        sa = sub.sa;
    }

    void walk_res(bool align, const rust_fn *dtor, uint16_t n_ty_params,
                  const uint8_t *ty_params_sp) {
        abort();    // TODO
    }

    template<typename T>
    void walk_number(bool align) { sa.set(sizeof(T), ALIGNOF(T)); }

    void compute_tag_size(tag_info &tinfo);

    template<typename T>
    static void compute_tag_size(const ctxt<T> &other_cx, tag_info &tinfo) {
        size_of cx(other_cx);
        cx.compute_tag_size(tinfo);
    }

    template<typename T>
    static size_align get(const ctxt<T> &other_cx, unsigned back_up = 0) {
        size_of cx(other_cx, other_cx.sp - back_up);
        cx.walk(false);
        assert(cx.sa.alignment > 0);
        return cx.sa;
    }
};


// Pointer wrappers for data traversals

class ptr {
private:
    uint8_t *p;

public:
    template<typename T>
    struct data { typedef T t; };

    ptr(uint8_t *in_p)
    : p(in_p) {}

    ptr(uintptr_t in_p)
    : p((uint8_t *)in_p) {}

    inline ptr operator+(const size_t amount) const {
        return make(p + amount);
    }
    inline ptr &operator+=(const size_t amount) { p += amount; return *this; }
    inline bool operator<(const ptr other) { return p < other.p; }
    inline ptr operator++() { ptr rv(*this); p++; return rv; }
    inline uint8_t operator*() { return *p; }

    template<typename T>
    inline operator T *() { return (T *)p; }

    inline operator uintptr_t() { return (uintptr_t)p; }

    static inline ptr make(uint8_t *in_p) {
        ptr self(in_p);
        return self;
    }
};

template<typename T>
static inline T
bump_dp(ptr &dp) {
    T x = *((T *)dp);
    dp += sizeof(T);
    return x;
}

template<typename T>
static inline T
get_dp(ptr dp) {
    return *((T *)dp);
}


// Pointer pairs for structural comparison

template<typename T>
class data_pair {
public:
    T fst, snd;

    data_pair() {}
    data_pair(T &in_fst, T &in_snd) : fst(in_fst), snd(in_snd) {}

    inline void operator=(const T rhs) { fst = snd = rhs; }

    static data_pair<T> make(T &fst, T &snd) {
        data_pair<T> data(fst, snd);
        return data;
    }
};

class ptr_pair {
public:
    uint8_t *fst, *snd;

    template<typename T>
    struct data { typedef data_pair<T> t; };

    ptr_pair(uint8_t *in_fst, uint8_t *in_snd) : fst(in_fst), snd(in_snd) {}

    ptr_pair(data_pair<uint8_t *> &other) : fst(other.fst), snd(other.snd) {}

    inline void operator=(uint8_t *rhs) { fst = snd = rhs; }

    inline ptr_pair operator+(size_t n) const {
        return make(fst + n, snd + n);
    }

    inline ptr_pair operator+=(size_t n) {
        fst += n; snd += n;
        return *this;
    }

    inline ptr_pair operator-(size_t n) const {
        return make(fst - n, snd - n);
    }

    inline bool operator<(const ptr_pair &other) const {
        return fst < other.fst && snd < other.snd;
    }

    static inline ptr_pair make(uint8_t *fst, uint8_t *snd) {
        ptr_pair self(fst, snd);
        return self;
    }

    static inline ptr_pair make(const data_pair<uint8_t *> &pair) {
        ptr_pair self(pair.fst, pair.snd);
        return self;
    }
};

// NB: This function does not align.
template<typename T>
inline data_pair<T>
bump_dp(ptr_pair &ptr) {
    data_pair<T> data(*reinterpret_cast<T *>(ptr.fst),
                      *reinterpret_cast<T *>(ptr.snd));
    ptr += sizeof(T);
    return data;
}

template<typename T>
inline data_pair<T>
get_dp(ptr_pair &ptr) {
    data_pair<T> data(*reinterpret_cast<T *>(ptr.fst),
                      *reinterpret_cast<T *>(ptr.snd));
    return data;
}

}   // end namespace shape


inline shape::ptr_pair
align_to(const shape::ptr_pair &pair, size_t n) {
    return shape::ptr_pair::make(align_to(pair.fst, n),
                                 align_to(pair.snd, n));
}


namespace shape {

// An abstract class (again using the curiously recurring template pattern)
// for methods that actually manipulate the data involved.

#define DATA_SIMPLE(ty, call) \
    if (align) dp = align_to(dp, sizeof(ty)); \
    U end_dp = dp + sizeof(ty); \
    static_cast<T *>(this)->call; \
    dp = end_dp;

template<typename T,typename U>
class data : public ctxt< data<T,U> > {
protected:
    void walk_box_contents(bool align);
    void walk_variant(bool align, tag_info &tinfo, uint32_t variant);

    static std::pair<uint8_t *,uint8_t *> get_evec_data_range(ptr dp);
    static std::pair<uint8_t *,uint8_t *> get_ivec_data_range(ptr dp);
    static std::pair<ptr_pair,ptr_pair> get_evec_data_range(ptr_pair &dp);
    static std::pair<ptr_pair,ptr_pair> get_ivec_data_range(ptr_pair &dp);

public:
    U dp;

    data(rust_task *in_task,
         const uint8_t *in_sp,
         const type_param *in_params,
         const rust_shape_tables *in_tables,
         U const &in_dp)
    : ctxt< data<T,U> >(in_task, in_sp, in_params, in_tables), dp(in_dp) {}

    void walk_tag(bool align, tag_info &tinfo);
    void walk_ivec(bool align, bool is_pod, size_align &elem_sa);

    void walk_struct(bool align, const uint8_t *end_sp) {
        static_cast<T *>(this)->walk_struct(align, end_sp);
    }

    void walk_evec(bool align, bool is_pod, uint16_t sp_size) {
        DATA_SIMPLE(void *, walk_evec(align, is_pod, sp_size));
    }

    void walk_box(bool align)   { DATA_SIMPLE(void *, walk_box(align)); }
    void walk_port(bool align)  { DATA_SIMPLE(void *, walk_port(align)); }
    void walk_chan(bool align)  { DATA_SIMPLE(void *, walk_chan(align)); }
    void walk_task(bool align)  { DATA_SIMPLE(void *, walk_task(align)); }

    void walk_fn(bool align) {
        if (align) dp = align_to(dp, sizeof(void *));
        static_cast<T *>(this)->walk_fn(align);
        dp += sizeof(void *) * 2;
    }

    void walk_obj(bool align) {
        if (align) dp = align_to(dp, sizeof(void *));
        static_cast<T *>(this)->walk_obj(align);
        dp += sizeof(void *) * 2;
    }

    void walk_res(bool align, const rust_fn *dtor, uint16_t n_ty_params,
                  const uint8_t *ty_params_sp) {
        // Delegate to the implementation.
        static_cast<T *>(this)->walk_res(align, dtor, n_ty_params,
                                         ty_params_sp);
    }

    void walk_var(bool align, uint8_t param_index) {
        const type_param *param = &this->params[param_index];
        T sub(*static_cast<T *>(this), param->shape, param->params,
              param->tables);
        static_cast<T *>(this)->walk_subcontext(align, sub);
        dp = sub.dp;
    }

    template<typename W>
    void walk_number(bool align) { DATA_SIMPLE(W, walk_number<W>()); }
};

template<typename T,typename U>
void
data<T,U>::walk_box_contents(bool align) {
    typename U::template data<uint8_t *>::t box_ptr = bump_dp<uint8_t *>(dp);

    U ref_count_dp(box_ptr);
    T sub(*static_cast<T *>(this), ref_count_dp + sizeof(uint32_t));
    static_cast<T *>(this)->walk_box_contents(align, sub, ref_count_dp);
}

template<typename T,typename U>
void
data<T,U>::walk_variant(bool align, tag_info &tinfo, uint32_t variant_id) {
    std::pair<const uint8_t *,const uint8_t *> variant_ptr_and_end =
        this->get_variant_sp(tinfo, variant_id);
    static_cast<T *>(this)->walk_variant(align, tinfo, variant_id,
                                         variant_ptr_and_end);
}

template<typename T,typename U>
std::pair<uint8_t *,uint8_t *>
data<T,U>::get_evec_data_range(ptr dp) {
    rust_evec *vp = bump_dp<rust_evec *>(dp);
    return std::make_pair(vp->data, vp->data + vp->fill);
}

template<typename T,typename U>
std::pair<uint8_t *,uint8_t *>
data<T,U>::get_ivec_data_range(ptr dp) {
    size_t fill = bump_dp<size_t>(dp);
    bump_dp<size_t>(dp);    // Skip over alloc.
    uint8_t *payload_dp = dp;
    rust_ivec_payload payload = bump_dp<rust_ivec_payload>(dp);

    uint8_t *start, *end;
    if (!fill) {
        if (!payload.ptr) {             // Zero length.
            start = end = NULL;
        } else {                        // On heap.
            fill = payload.ptr->fill;
            start = payload.ptr->data;
            end = start + fill;
        }
    } else {                            // On stack.
        start = payload_dp;
        end = start + fill;
    }

    return std::make_pair(start, end);
}

template<typename T,typename U>
std::pair<ptr_pair,ptr_pair>
data<T,U>::get_evec_data_range(ptr_pair &dp) {
    std::pair<uint8_t *,uint8_t *> fst = get_evec_data_range(dp.fst);
    std::pair<uint8_t *,uint8_t *> snd = get_evec_data_range(dp.snd);
    ptr_pair start(fst.first, snd.first);
    ptr_pair end(fst.second, snd.second);
    return std::make_pair(start, end);
}

template<typename T,typename U>
std::pair<ptr_pair,ptr_pair>
data<T,U>::get_ivec_data_range(ptr_pair &dp) {
    std::pair<uint8_t *,uint8_t *> fst = get_ivec_data_range(dp.fst);
    std::pair<uint8_t *,uint8_t *> snd = get_ivec_data_range(dp.snd);
    ptr_pair start(fst.first, snd.first);
    ptr_pair end(fst.second, snd.second);
    return std::make_pair(start, end);
}

template<typename T,typename U>
void
data<T,U>::walk_ivec(bool align, bool is_pod, size_align &elem_sa) {
    if (!elem_sa.is_set())
        elem_sa = size_of::get(*this);
    else if (elem_sa.alignment == 8)
        elem_sa.alignment = 4;  // FIXME: This is an awful hack.

    // Get a pointer to the interior vector, and determine its size.
    if (align) dp = align_to(dp, ALIGNOF(rust_ivec *));
    U end_dp = dp + sizeof(rust_ivec) - sizeof(uintptr_t) + elem_sa.size * 4;

    // Call to the implementation.
    static_cast<T *>(this)->walk_ivec(align, is_pod, elem_sa);

    dp = end_dp;
}

template<typename T,typename U>
void
data<T,U>::walk_tag(bool align, tag_info &tinfo) {
    size_of::compute_tag_size(*this, tinfo);

    if (tinfo.variant_count > 1 && align)
        dp = align_to(dp, ALIGNOF(uint32_t));

    U end_dp = dp + tinfo.tag_sa.size;

    typename U::template data<uint32_t>::t tag_variant;
    if (tinfo.variant_count > 1)
        tag_variant = bump_dp<uint32_t>(dp);
    else
        tag_variant = 0;

    static_cast<T *>(this)->walk_tag(align, tinfo, tag_variant);

    dp = end_dp;
}


// Polymorphic logging, for convenience

class log : public data<log,ptr> {
    friend class data<log,ptr>;

private:
    std::ostream &out;
    bool in_string;

    log(log &other,
        const uint8_t *in_sp,
        const type_param *in_params,
        const rust_shape_tables *in_tables = NULL)
    : data<log,ptr>(other.task,
                    in_sp,
                    in_params,
                    in_tables ? in_tables : other.tables,
                    other.dp),
      out(other.out) {}

    log(log &other, ptr in_dp)
    : data<log,ptr>(other.task, other.sp, other.params, other.tables, in_dp),
      out(other.out) {}

    void walk_string(const std::pair<ptr,ptr> &data);

    void walk_evec(bool align, bool is_pod, uint16_t sp_size) {
        walk_vec(align, is_pod, get_evec_data_range(dp));
    }

    void walk_ivec(bool align, bool is_pod, size_align &elem_sa) {
        walk_vec(align, is_pod, get_ivec_data_range(dp));
    }

    void walk_tag(bool align, tag_info &tinfo, uint32_t tag_variant) {
        out << "tag" << tag_variant;
        data<log,ptr>::walk_variant(align, tinfo, tag_variant);
    }

    void walk_box(bool align) {
        out << "@";
        data<log,ptr>::walk_box_contents(align);
    }

    void walk_fn(bool align) { out << "fn"; }
    void walk_obj(bool align) { out << "obj"; }
    void walk_port(bool align) { out << "port"; }
    void walk_chan(bool align) { out << "chan"; }
    void walk_task(bool align) { out << "task"; }

    void walk_res(bool align, const rust_fn *dtor, uint16_t n_ty_params,
                  const uint8_t *ty_params_sp) {
        out << "res";   // TODO
    }

    void walk_subcontext(bool align, log &sub) { sub.walk(align); }

    void walk_box_contents(bool align, log &sub, ptr &ref_count_dp) {
        if (ref_count_dp == 0)
            out << "(null)";
        else
            sub.walk(true);
    }

    void walk_struct(bool align, const uint8_t *end_sp);
    void walk_vec(bool align, bool is_pod, const std::pair<ptr,ptr> &data);
    void walk_variant(bool align, tag_info &tinfo, uint32_t variant_id,
                      const std::pair<const uint8_t *,const uint8_t *>
                      variant_ptr_and_end);

    template<typename T>
    void walk_number() { out << get_dp<T>(dp); }

public:
    log(rust_task *in_task,
        const uint8_t *in_sp,
        const type_param *in_params,
        const rust_shape_tables *in_tables,
        uint8_t *in_data,
        std::ostream &in_out)
    : data<log,ptr>(in_task, in_sp, in_params, in_tables, in_data),
      out(in_out) {}
};

}   // end namespace shape

#endif
