/**
 * Autogenerated by Thrift
 *
 * DO NOT EDIT UNLESS YOU ARE SURE THAT YOU KNOW WHAT YOU ARE DOING
 *  @generated
 */
#pragma once

#include <thrift/lib/cpp/Thrift.h>
#include <thrift/lib/cpp/TApplicationException.h>
#include <thrift/lib/cpp/protocol/TProtocol.h>
#include <thrift/lib/cpp/transport/TTransport.h>

namespace apache { namespace thrift { namespace reflection {
class Schema;
}}}




enum MyEnum {
  MyValue1 = 0,
  MyValue2 = 1,
};

extern const typename apache::thrift::detail::TEnumMapFactory<MyEnum, int>::ValuesToNamesMapType _MyEnum_VALUES_TO_NAMES;

extern const typename apache::thrift::detail::TEnumMapFactory<MyEnum, int>::NamesToValuesMapType _MyEnum_NAMES_TO_VALUES;


namespace apache { namespace thrift {
template<>
struct TEnumTraits< ::MyEnum> : public TEnumTraitsBase< ::MyEnum>
{
inline static constexpr  ::MyEnum min() {
return  ::MyEnum::MyValue1;
}
inline static constexpr  ::MyEnum max() {
return  ::MyEnum::MyValue2;
}
};
}} // apache:thrift


class MyStruct;

void swap(MyStruct &a, MyStruct &b);

class MyStruct : public apache::thrift::TStructType<MyStruct> {
 public:

  static const uint64_t _reflection_id = 7958971832214294220U;
  static void _reflection_register(::apache::thrift::reflection::Schema&);
  MyStruct() : MyIntField(0) {
  }
  template <
    typename T__ThriftWrappedArgument__Ctor,
    typename... Args__ThriftWrappedArgument__Ctor
  >
  explicit MyStruct(
    ::apache::thrift::detail::argument_wrapper<1, T__ThriftWrappedArgument__Ctor> arg,
    Args__ThriftWrappedArgument__Ctor&&... args
  ):
    MyStruct(std::forward<Args__ThriftWrappedArgument__Ctor>(args)...)
  {
    MyIntField = arg.move();
    __isset.MyIntField = true;
  }
  template <
    typename T__ThriftWrappedArgument__Ctor,
    typename... Args__ThriftWrappedArgument__Ctor
  >
  explicit MyStruct(
    ::apache::thrift::detail::argument_wrapper<2, T__ThriftWrappedArgument__Ctor> arg,
    Args__ThriftWrappedArgument__Ctor&&... args
  ):
    MyStruct(std::forward<Args__ThriftWrappedArgument__Ctor>(args)...)
  {
    MyStringField = arg.move();
    __isset.MyStringField = true;
  }

  MyStruct(const MyStruct&) = default;
  MyStruct& operator=(const MyStruct& src)= default;
  MyStruct(MyStruct&&) = default;
  MyStruct& operator=(MyStruct&&) = default;

  void __clear();

  virtual ~MyStruct() throw() {}

  int64_t MyIntField;
  std::string MyStringField;

  struct __isset {
    __isset() { __clear(); } 
    void __clear() {
      MyIntField = false;
      MyStringField = false;
    }
    bool MyIntField;
    bool MyStringField;
  } __isset;

  bool operator == (const MyStruct &) const;
  bool operator != (const MyStruct& rhs) const {
    return !(*this == rhs);
  }

  bool operator < (const MyStruct & ) const;

  template <class Protocol_>
  uint32_t read(Protocol_* iprot);
  template <class Protocol_>
  uint32_t write(Protocol_* oprot) const;

};

class MyStruct;
void merge(const MyStruct& from, MyStruct& to);
void merge(MyStruct&& from, MyStruct& to);


#include "thrift/compiler/test/fixtures/basic-cpp-async/gen-cpp/module_types.tcc"
