/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <cstdlib>
#include <cstring>
#include <functional>
#include <list>
#include <map>
#include <sstream>
#include <string>

#include "DexIdx.h"
#include "dexdefs.h"
#include "DexAccess.h"
#include "DexInstruction.h"
#include "DexAnnotation.h"
#include "Show.h"
#include "Trace.h"
#include "RedexContext.h"
#include "ReferencedState.h"

/*
 * The structures defined here are literal representations
 * of what can be represented in a dex.  The main purpose of
 * the translations present here are to decouple the limitations
 * of "Idx" representation.  All of the "Idx"'s are indexes into
 * arrays of types in the dex format.  They are specific to each
 * dexfile.  So, we transform them in a way that we can load
 * multiple dexes in memory and compare them symbolically.
 *
 * In doing so, we enforce the uniqueness requirements of Idx's
 * within dexes.  There's only one DexString* with the same
 * set of characters.  Only one DexType* that has name "Foo;".
 * That simplifies the process of re-marshalling to dex after
 * we've completed whatever transforms we are going to do.
 *
 * UNIQUENESS:
 * The private constructor pattern enforces the uniqueness of
 * the pointer values of each type that has a uniqueness requirement.
 */

class DexDebugInstruction;
class DexOutputIdx;
class DexString;
class DexType;

class DexString {
  friend struct RedexContext;

  const char* m_cstr;
  int m_utfsize;
  int m_strlen;

  // See UNIQUENESS above for the rationale for the private constructor pattern.
  DexString(const char* nstr, int utfsize) {
    m_cstr = (const char*)strdup(nstr);
    m_utfsize = utfsize;
    m_strlen = strlen(nstr);
  }

  ~DexString() {
    free(const_cast<char*>(m_cstr));
  }

 public:
  // DexString retrieval/creation

  // If the DexString exists, return it, otherwise create it and return it.
  // See also get_string()
  static DexString* make_string(const char* nstr, int utfsize) {
    return g_redex->make_string(nstr, utfsize);
  }

  static DexString* make_string(const char* nstr) {
    return make_string(nstr, strlen(nstr));
  }

  // Return an existing DexString or nullptr if one does not exist.
  static DexString* get_string(const char* nstr, int utfsize) {
    return g_redex->get_string(nstr, utfsize);
  }

  static DexString* get_string(const char* nstr) {
    return get_string(nstr, strlen(nstr));
  }

 public:
  bool is_simple() const {
    if (m_strlen == m_utfsize) return true;
    return false;
  }

  const char* c_str() const { return m_cstr; }

  int get_entry_size() const {
    int len = uleb128_encoding_size(m_utfsize);
    len += m_strlen;
    len++; // NULL byte
    return len;
  }

  void encode(uint8_t* output) {
    output = write_uleb128(output, m_utfsize);
    strcpy((char*)output, m_cstr);
  }

  template <typename V>
  static void visit_all_dexstring(V v);

  friend std::string show(const DexString*);
};

/* Non-optimizing DexSpec compliant ordering */
inline bool compare_dexstrings(const DexString* a, const DexString* b) {
  if (a->is_simple() && b->is_simple())
    return (strcmp(a->c_str(), b->c_str()) < 0);
  /*
   * Bother, need to do code-point character-by-character
   * comparison.
   */
  const char* sa = a->c_str();
  const char* sb = b->c_str();
  /* Equivalence test first, so we don't worry about walking
   * off the end.
   */
  if (strcmp(sa, sb) == 0) return false;
  if (strlen(sa) == 0) {
    return true;
  }
  if (strlen(sb) == 0) {
    return false;
  }
  while (1) {
    uint32_t cpa = mutf8_next_code_point(sa);
    uint32_t cpb = mutf8_next_code_point(sb);
    if (cpa == cpb) {
      if (*sa == '\0') return true;
      if (*sb == '\0') return false;
      continue;
    }
    return (cpa < cpb);
  }
}

class DexType {
  friend struct RedexContext;

  DexString* m_name;

  // See UNIQUENESS above for the rationale for the private constructor pattern.
  DexType(DexString* dstring) {
    m_name = dstring;
  }

 public:
  // DexType retrieval/creation

  // If the DexType exists, return it, otherwise create it and return it.
  // See also get_type()
  static DexType* make_type(DexString* dstring) {
    return g_redex->make_type(dstring);
  }

  static DexType* make_type(const char* type_string) {
    return make_type(DexString::make_string(type_string));
  }

  static DexType* make_type(const char* type_string, int utfsize) {
    return make_type(DexString::make_string(type_string, utfsize));
  }

  // Return an existing DexType or nullptr if one does not exist.
  static DexType* get_type(DexString* dstring) {
    return g_redex->get_type(dstring);
  }

  static DexType* get_type(const char* type_string) {
    return get_type(DexString::get_string(type_string));
  }

  static DexType* get_type(const char* type_string, int utfsize) {
    return get_type(DexString::get_string(type_string, utfsize));
  }

 public:
  void assign_name_alias(DexString* new_name) {
    g_redex->alias_type_name(this, new_name);
  }

  DexString* get_name() const { return m_name; }

  friend std::string show(const DexType*);

  template <typename V>
  static void visit_all_dextype(V v);
};

/* Non-optimizing DexSpec compliant ordering */
inline bool compare_dextypes(const DexType* a, const DexType* b) {
  return compare_dexstrings(a->get_name(), b->get_name());
}

class DexField {
  friend struct RedexContext;

  DexType* m_class; // Field inside of class m_class.
  DexString* m_name;
  DexType* m_type; // Field of type m_type.
  /* Concrete method members */
  DexAnnotationSet* m_anno;
  DexEncodedValue* m_value; /* Static Only */
  DexAccessFlags m_access;
  bool m_concrete;
  bool m_external;

  // See UNIQUENESS above for the rationale for the private constructor pattern.
  DexField(DexType* container, DexString* name, DexType* type) {
    m_concrete = false;
    m_external = false;
    m_anno = nullptr;
    m_value = nullptr;
    m_class = container;
    m_name = name;
    m_type = type;
    m_access = static_cast<DexAccessFlags>(0);
  }

 public:
  ReferencedState rstate; // Tracks whether this field can be deleted or renamed

  // DexField retrieval/creation

  // If the DexField exists, return it, otherwise create it and return it.
  // See also get_field()
  static DexField* make_field(DexType* container,
                              DexString* name,
                              DexType* type) {
    return g_redex->make_field(container, name, type);
  }

  // Return an existing DexField or nullptr if one does not exist.
  static DexField* get_field(DexType* container,
                             DexString* name,
                             DexType* type) {
    return g_redex->get_field(container, name, type);
  }

 public:
  DexAnnotationSet* get_anno_set() const { return m_anno; }
  DexEncodedValue* get_static_value() { return m_value; }
  DexType* get_class() const { return m_class; }
  DexString* get_name() const { return m_name; }
  DexType* get_type() const { return m_type; }
  bool is_def() const { return is_concrete() || is_external(); }
  DexAccessFlags get_access() const {
    always_assert(is_def());
    return m_access;
  }
  bool is_concrete() const { return m_concrete; }
  bool is_external() const { return m_external; }

  void set_access(DexAccessFlags access) { assert(!m_external); m_access = access; }
  void set_external() {
    always_assert(!m_concrete);
    m_external = true;
  }
  void make_concrete(DexAccessFlags access_flags, DexEncodedValue* v = nullptr);
  void clear_annotations() {
    delete m_anno;
    m_anno = nullptr;
  }

  void attach_annotation_set(DexAnnotationSet* aset) {
    if (m_anno == nullptr && m_concrete == false) {
      m_anno = aset;
      return;
    }
    always_assert_log(false, "attach_annotation_set failed for field %s.%s\n",
                      m_class->get_name()->c_str(),
                      m_name->c_str());
  }

  void gather_types_shallow(std::vector<DexType*>& ltype);
  void gather_strings_shallow(std::vector<DexString*>& lstring);

  void gather_types(std::vector<DexType*>& ltype);
  void gather_strings(std::vector<DexString*>& lstring);
  void gather_fields(std::vector<DexField*>& lfield);
  void gather_methods(std::vector<DexMethod*>& lmethod);

  friend std::string show(const DexField*);
};

/* Non-optimizing DexSpec compliant ordering */
inline bool compare_dexfields(const DexField* a, const DexField* b) {
  if (a->get_class() != b->get_class()) {
    return compare_dextypes(a->get_class(), b->get_class());
  }
  if (a->get_name() != b->get_name()) {
    return compare_dexstrings(a->get_name(), b->get_name());
  }
  return compare_dextypes(a->get_type(), b->get_type());
}

class DexTypeList {
  friend struct RedexContext;

  std::list<DexType*> m_list;

  // See UNIQUENESS above for the rationale for the private constructor pattern.
  DexTypeList(std::list<DexType*>&& p) {
    m_list = std::move(p);
  }

 public:
  // DexTypeList retrieval/creation

  // If the DexTypeList exists, return it, otherwise create it and return it.
  // See also get_type_list()
  static DexTypeList* make_type_list(std::list<DexType*>&& p) {
    return g_redex->make_type_list(std::move(p));
  }

  // Return an existing DexTypeList or nullptr if one does not exist.
  static DexTypeList* get_type_list(std::list<DexType*>&& p) {
    return g_redex->get_type_list(std::move(p));
  }

 public:
  const std::list<DexType*>& get_type_list() const { return m_list; }
  /**
   * Returns size of the encoded typelist in bytes, input
   * pointer must be aligned.
   */
  int encode(DexOutputIdx* dodx, uint32_t* output);
  friend bool operator<(DexTypeList& a, DexTypeList& b) {
    std::list<DexType*>::iterator ita = a.m_list.begin();
    std::list<DexType*>::iterator itb = b.m_list.begin();
    while (1) {
      if (itb == b.m_list.end()) return false;
      if (ita == a.m_list.end()) return true;
      if (*ita != *itb) {
        const DexType* ta = *ita;
        const DexType* tb = *itb;
        return compare_dextypes(ta, tb);
      }
      ita++;
      itb++;
    }
  }

  void gather_types(std::vector<DexType*>& ltype);

  friend std::string show(const DexTypeList*);
};

inline bool compare_dextypelists(DexTypeList* a, DexTypeList* b) {
  return *a < *b;
}

class DexProto {
  friend struct RedexContext;

  DexTypeList* m_args;
  DexType* m_rtype;
  DexString* m_shorty;

  // See UNIQUENESS above for the rationale for the private constructor pattern.
  DexProto(DexType* rtype, DexTypeList* args, DexString* shorty) {
    m_rtype = rtype;
    m_args = args;
    m_shorty = shorty;
  }

 public:
  // DexProto retrieval/creation

  // If the DexProto exists, return it, otherwise create it and return it.
  // See also get_proto()
  static DexProto* make_proto(DexType* rtype,
                              DexTypeList* args,
                              DexString* shorty) {
    return g_redex->make_proto(rtype, args, shorty);
  }

  static DexProto* make_proto(DexType* rtype, DexTypeList* args);

  // Return an existing DexProto or nullptr if one does not exist.
  static DexProto* get_proto(DexType* rtype, DexTypeList* args) {
    return g_redex->get_proto(rtype, args);
  }

 public:
  DexType* get_rtype() const { return m_rtype; }
  DexTypeList* get_args() const { return m_args; }
  DexString* get_shorty() const { return m_shorty; }

  void gather_types(std::vector<DexType*>& ltype);
  void gather_strings(std::vector<DexString*>& lstring);

  friend std::string show(const DexProto*);
};

/* Non-optimizing DexSpec compliant ordering */
inline bool compare_dexprotos(const DexProto* a, const DexProto* b) {
  if (a->get_rtype() != b->get_rtype()) {
    return compare_dextypes(a->get_rtype(), b->get_rtype());
  }
  return (*(a->get_args()) < *(b->get_args()));
}

class DexDebugItem {
  uint32_t m_line_start;
  std::vector<DexString*> m_param_names;
  std::vector<DexDebugInstruction*> m_insns;
  DexDebugItem(DexIdx* idx, uint32_t offset);

 public:
  static DexDebugItem* get_dex_debug(DexIdx* idx, uint32_t offset);

 public:
  ~DexDebugItem();

  std::vector<DexDebugInstruction*>& get_instructions() { return m_insns; }
  uint32_t get_line_start() const { return m_line_start; }

  /* Returns number of bytes encoded, *output has no alignment requirements */
  int encode(DexOutputIdx* dodx, uint8_t* output);

  void gather_types(std::vector<DexType*>& ltype);
  void gather_strings(std::vector<DexString*>& lstring);
};

typedef std::vector<std::pair<DexType*, uint32_t>> DexCatches;

struct DexTryItem {
  uint32_t m_start_addr;
  uint32_t m_insn_count;
  /* Order matters for m_catches, so using list<> instead of map<> */
  DexCatches m_catches; /* Gathered by DexCode */
  uint32_t m_catchall; /* DEX_NO_INDEX if none */
};

class DexCode {
  uint16_t m_registers_size;
  uint16_t m_ins_size;
  uint16_t m_outs_size;
  std::vector<DexInstruction*> m_insns;
  std::vector<DexTryItem*> m_tries;
  DexDebugItem* m_dbg;

 public:
  static DexCode* get_dex_code(DexIdx* idx, uint32_t offset);

  // TODO: make it private and find a better way to allow code creation
  DexCode()
     : m_registers_size(0)
     , m_ins_size(0)
     , m_outs_size(0)
     , m_dbg(nullptr) {}

  ~DexCode() {
    for (auto const& op : m_insns) {
      delete op;
    }
    for (auto const& ti : m_tries) {
      delete ti;
    }
    delete m_dbg;
  }

 public:
  DexDebugItem* get_debug_item() const { return m_dbg; }
  std::vector<DexInstruction*>& get_instructions() { return m_insns; }
  const std::vector<DexInstruction*>& get_instructions() const { return m_insns; }
  std::vector<DexTryItem*>& get_tries() { return m_tries; }
  const std::vector<DexTryItem*>& get_tries() const { return m_tries; }
  uint16_t get_registers_size() const { return m_registers_size; }
  uint16_t get_ins_size() const { return m_ins_size; }
  uint16_t get_outs_size() const { return m_outs_size; }

  void set_registers_size(uint16_t sz) { m_registers_size = sz; }
  void set_ins_size(uint16_t sz) { m_ins_size = sz; }
  void set_outs_size(uint16_t sz) { m_outs_size = sz; }
  void set_debug_item(DexDebugItem* dbg) { m_dbg = dbg; }

  /*
   * Returns number of bytes in encoded output, passed in
   * pointer must be aligned.  Does not encode debugitem,
   * that must be done later.
   */
  int encode(DexOutputIdx* dodx, uint32_t* output);

  void gather_types(std::vector<DexType*>& ltype);
  void gather_catch_types(std::vector<DexType*>& ltype);
  void gather_strings(std::vector<DexString*>& lstring);
  void gather_fields(std::vector<DexField*>& lfield);
  void gather_methods(std::vector<DexMethod*>& lmethod);

  friend std::string show(const DexCode*);
};

class DexMethod {
  friend struct RedexContext;

  /* Method Ref related members */
  DexType* m_class;
  DexString* m_name;
  DexProto* m_proto;
  /* Concrete method members */
  DexAnnotationSet* m_anno;
  DexCode* m_code;
  DexAccessFlags m_access;
  bool m_concrete;
  bool m_virtual;
  bool m_external;
  ParamAnnotations m_param_anno;

  // See UNIQUENESS above for the rationale for the private constructor pattern.
  DexMethod(DexType* type, DexString* name, DexProto* proto) {
    m_concrete = false;
    m_virtual = false;
    m_external = false;
    m_anno = nullptr;
    m_code = nullptr;
    m_class = type;
    m_name = name;
    m_proto = proto;
    m_access = static_cast<DexAccessFlags>(0);
  }

  ~DexMethod() {
    delete m_code;
  }

 public:
  // Tracks whether this method can be deleted or renamed
  ReferencedState rstate;

  // DexMethod retrieval/creation

  // If the DexMethod exists, return it, otherwise create it and return it.
  // See also get_method()
  static DexMethod* make_method(DexType* type,
                                DexString* name,
                                DexProto* proto) {
    return g_redex->make_method(type, name, proto);
  }

  /**
   * This creates everything along the chain of Dex<Member>, so it should
   * be used for members that either exist or would be created anyway.
   */
  static DexMethod* make_method(const char* cls_name,
                                const char* meth_name,
                                const char* rtype_str,
                                std::vector<const char*> arg_strs) {
    DexType* cls = DexType::make_type(cls_name);
    DexString* name = DexString::make_string(meth_name);
    DexType* rtype = DexType::make_type(rtype_str);
    std::list<DexType*> args;
    for (auto const arg_str : arg_strs) {
      DexType* arg = DexType::make_type(arg_str);
      args.push_back(arg);
    }
    DexTypeList* dtl = DexTypeList::make_type_list(std::move(args));
    return make_method(cls, name, DexProto::make_proto(rtype, dtl));
  }

  // Return an existing DexMethod or nullptr if one does not exist.
  static DexMethod* get_method(DexType* type,
                               DexString* name,
                               DexProto* proto) {
    return g_redex->get_method(type, name, proto);
  }

 public:
  const DexAnnotationSet* get_anno_set() const { return m_anno; }
  DexAnnotationSet* get_anno_set() { return m_anno; }
  DexType* get_class() const { return m_class; }
  DexString* get_name() const { return m_name; }
  DexProto* get_proto() const { return m_proto; }
  DexCode* get_code() const { return m_code; }
  bool is_concrete() const { return m_concrete; }
  bool is_virtual() const { return m_virtual; }
  bool is_external() const { return m_external; }
  bool is_def() const {
    return is_concrete() || is_external();
  }
  DexAccessFlags get_access() const {
    always_assert(is_def());
    return m_access;
  }
  ParamAnnotations* get_param_anno() {
    if (m_param_anno.size() == 0) return nullptr;
    return &m_param_anno;
  }

  void set_access(DexAccessFlags access) { assert(!m_external); m_access = access; }
  void set_virtual(bool is_virtual) { assert(!m_external); m_virtual = is_virtual; }
  void set_external() {
    always_assert(!m_concrete);
    m_external = true;
  }
  void set_code(DexCode* code) { m_code = code; }

  void make_concrete(DexAccessFlags access, DexCode* dc, bool is_virtual);
  void change_class(DexType* cls) {
    g_redex->mutate_method_class(this, cls);
  }
  void change_proto(DexProto* proto) {
    g_redex->mutate_method_proto(this, proto);
  }
  void become_virtual();
  void clear_annotations() {
    delete m_anno;
    m_anno = nullptr;
  }
  void attach_annotation_set(DexAnnotationSet* aset) {
    if (m_anno == nullptr && m_concrete == false) {
      m_anno = aset;
      return;
    }
    always_assert_log(false, "attach_annotation_set failed for method %s\n",
                      show_short(this).c_str());
  }
  void attach_param_annotation_set(int paramno, DexAnnotationSet* aset) {
    if (m_param_anno.count(paramno) == 0 && m_concrete == false) {
      m_param_anno[paramno] = aset;
      return;
    }
    always_assert_log(false, "attach_param_annotation_set failed for param %d "
                      "to method %s\n",
                      paramno, show_short(this).c_str());
  }

  void gather_types_shallow(std::vector<DexType*>& ltype);
  void gather_strings_shallow(std::vector<DexString*>& lstring);

  void gather_types(std::vector<DexType*>& ltype);
  void gather_fields(std::vector<DexField*>& lfield);
  void gather_methods(std::vector<DexMethod*>& lmethod);
  void gather_strings(std::vector<DexString*>& lstring);

  friend std::string show(const DexMethod*);
  friend std::string show_short(const DexMethod*);
};

/* Non-optimizing DexSpec compliant ordering */
inline bool compare_dexmethods(const DexMethod* a, const DexMethod* b) {
  if (a->get_class() != b->get_class()) {
    return compare_dextypes(a->get_class(), b->get_class());
  }
  if (a->get_name() != b->get_name()) {
    return compare_dexstrings(a->get_name(), b->get_name());
  }
  return compare_dexprotos(a->get_proto(), b->get_proto());
}

typedef std::map<DexCode*, uint32_t> dexcode_to_offset;

class DexClass {
 private:
  DexAccessFlags m_access_flags;
  DexType* m_super_class;
  DexType* m_self;
  DexTypeList* m_interfaces;
  DexString* m_source_file;
  DexAnnotationSet* m_anno;
  std::list<DexField*> m_sfields;
  std::list<DexField*> m_ifields;
  std::list<DexMethod*> m_dmethods;
  std::list<DexMethod*> m_vmethods;
  bool m_has_class_data;
  bool m_external;

  DexClass(){};
  void load_class_annotations(DexIdx* idx, uint32_t anno_off);
  void load_class_data_item(DexIdx* idx,
                            uint32_t cdi_off,
                            DexEncodedValueArray* svalues);

  friend struct ClassCreator;

 public:
  ReferencedState rstate;
  DexClass(DexIdx* idx, dex_class_def* cdef);

 public:
  const std::list<DexMethod*>& get_dmethods() const { return m_dmethods; }
  std::list<DexMethod*>& get_dmethods() { assert(!m_external); return m_dmethods; }
  const std::list<DexMethod*>& get_vmethods() const { return m_vmethods; }
  std::list<DexMethod*>& get_vmethods() { assert(!m_external); return m_vmethods; }
  const std::list<DexField*>& get_sfields() const { return m_sfields; }
  std::list<DexField*>& get_sfields() { assert(!m_external); return m_sfields; }
  const std::list<DexField*>& get_ifields() const { return m_ifields; }
  std::list<DexField*>& get_ifields() { assert(!m_external); return m_ifields; }
  DexAnnotationDirectory* get_annotation_directory();
  DexAccessFlags get_access() const { return m_access_flags; }
  DexType* get_super_class() const { return m_super_class; }
  DexType* get_type() const { return m_self; }
  DexString* get_name() const { return m_self->get_name(); }
  DexTypeList* get_interfaces() const { return m_interfaces; }
  DexString* get_source_file() const { return m_source_file; }
  bool has_class_data() const { return m_has_class_data; }
  bool is_external() const { return m_external; }
  DexEncodedValueArray* get_static_values();
  DexAnnotationSet* get_anno_set() const { return m_anno; }

  void set_access(DexAccessFlags access) { assert(!m_external); m_access_flags = access; }
  void set_source_file(DexString* source_file) { m_source_file = source_file; }
  void set_interfaces(DexTypeList* intfs) { assert(!m_external); m_interfaces = intfs; }
  void clear_annotations() {
    delete m_anno;
    m_anno = nullptr;
  }
  /* Encodes class_data_item, returns size in bytes.  No
   * alignment requirements on *output
   */
  int encode(DexOutputIdx* dodx, dexcode_to_offset& dco, uint8_t* output);

  void gather_types(std::vector<DexType*>& ltype);
  void gather_strings(std::vector<DexString*>& lstring);
  void gather_fields(std::vector<DexField*>& lfield);
  void gather_methods(std::vector<DexMethod*>& lmethod);

  friend std::string show(const DexClass*);
};

class DexClasses {
  std::vector<DexClass*> m_classes;

 public:
  using iterator = std::vector<DexClass*>::iterator;
  using const_iterator = std::vector<DexClass*>::const_iterator;

  DexClasses(int size) : m_classes(size) {}

  DexClasses(const DexClasses&) = delete;
  DexClasses(DexClasses&&) = default;

  void insert_at(DexClass* cls, int num) {
    m_classes.at(num) = cls;
  }

  DexClass* get(int num) {
    return m_classes.at(num);
  }

  iterator erase(iterator begin, iterator end) {
    return m_classes.erase(begin, end);
  }

  int size() { return m_classes.size(); }
  iterator begin() { return m_classes.begin(); }
  iterator end() { return m_classes.end(); }
  const_iterator begin() const { return m_classes.cbegin(); }
  const_iterator end() const { return m_classes.cend(); }
};
