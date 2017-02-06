/**
 * Autogenerated by Thrift
 *
 * DO NOT EDIT UNLESS YOU ARE SURE THAT YOU KNOW WHAT YOU ARE DOING
 *  @generated
 */
#include "thrift/compiler/test/fixtures/constants/gen-cpp/module_types.h"

#include "thrift/compiler/test/fixtures/constants/gen-cpp/module_reflection.h"

#include <algorithm>
#include <string.h>



const typename apache::thrift::detail::TEnumMapFactory<EmptyEnum, int>::ValuesToNamesMapType _EmptyEnum_VALUES_TO_NAMES = apache::thrift::detail::TEnumMapFactory<EmptyEnum, int>::makeValuesToNamesMap();

const typename apache::thrift::detail::TEnumMapFactory<EmptyEnum, int>::NamesToValuesMapType _EmptyEnum_NAMES_TO_VALUES = apache::thrift::detail::TEnumMapFactory<EmptyEnum, int>::makeNamesToValuesMap();


namespace apache { namespace thrift {
template<>
folly::Range<const std::pair< ::EmptyEnum, folly::StringPiece>*> TEnumTraitsBase< ::EmptyEnum>::enumerators() {
  return {};
}

template<>
const char* TEnumTraitsBase< ::EmptyEnum>::findName( ::EmptyEnum value) {
return findName( ::_EmptyEnum_VALUES_TO_NAMES, value);
}

template<>
bool TEnumTraitsBase< ::EmptyEnum>::findValue(const char* name,  ::EmptyEnum* out) {
return findValue( ::_EmptyEnum_NAMES_TO_VALUES, name, out);
}
}} // apache::thrift


const typename apache::thrift::detail::TEnumMapFactory<City, int>::ValuesToNamesMapType _City_VALUES_TO_NAMES = apache::thrift::detail::TEnumMapFactory<City, int>::makeValuesToNamesMap();

const typename apache::thrift::detail::TEnumMapFactory<City, int>::NamesToValuesMapType _City_NAMES_TO_VALUES = apache::thrift::detail::TEnumMapFactory<City, int>::makeNamesToValuesMap();


namespace apache { namespace thrift {
template<>
folly::Range<const std::pair< ::City, folly::StringPiece>*> TEnumTraitsBase< ::City>::enumerators() {
  static constexpr const std::pair< ::City, folly::StringPiece> storage[4] = {
    { ::City::NYC, "NYC"},
    { ::City::MPK, "MPK"},
    { ::City::SEA, "SEA"},
    { ::City::LON, "LON"},
  };
  return folly::range(storage);
}

template<>
const char* TEnumTraitsBase< ::City>::findName( ::City value) {
return findName( ::_City_VALUES_TO_NAMES, value);
}

template<>
bool TEnumTraitsBase< ::City>::findValue(const char* name,  ::City* out) {
return findValue( ::_City_NAMES_TO_VALUES, name, out);
}
}} // apache::thrift


const typename apache::thrift::detail::TEnumMapFactory<Company, int>::ValuesToNamesMapType _Company_VALUES_TO_NAMES = apache::thrift::detail::TEnumMapFactory<Company, int>::makeValuesToNamesMap();

const typename apache::thrift::detail::TEnumMapFactory<Company, int>::NamesToValuesMapType _Company_NAMES_TO_VALUES = apache::thrift::detail::TEnumMapFactory<Company, int>::makeNamesToValuesMap();


namespace apache { namespace thrift {
template<>
folly::Range<const std::pair< ::Company, folly::StringPiece>*> TEnumTraitsBase< ::Company>::enumerators() {
  static constexpr const std::pair< ::Company, folly::StringPiece> storage[4] = {
    { ::Company::FACEBOOK, "FACEBOOK"},
    { ::Company::WHATSAPP, "WHATSAPP"},
    { ::Company::OCULUS, "OCULUS"},
    { ::Company::INSTAGRAM, "INSTAGRAM"},
  };
  return folly::range(storage);
}

template<>
const char* TEnumTraitsBase< ::Company>::findName( ::Company value) {
return findName( ::_Company_VALUES_TO_NAMES, value);
}

template<>
bool TEnumTraitsBase< ::Company>::findValue(const char* name,  ::Company* out) {
return findValue( ::_Company_NAMES_TO_VALUES, name, out);
}
}} // apache::thrift


const uint64_t Internship::_reflection_id;
void Internship::_reflection_register(::apache::thrift::reflection::Schema& schema) {
   ::module_reflection_::reflectionInitializer_9022508676980868684(schema);
}

bool Internship::operator == (const Internship & rhs) const {
  if (!(this->weeks == rhs.weeks))
    return false;
  if (!(this->title == rhs.title))
    return false;
  if (__isset.employer != rhs.__isset.employer)
    return false;
  else if (__isset.employer && !(employer == rhs.employer))
    return false;
  return true;
}

uint32_t Internship::read(apache::thrift::protocol::TProtocol* iprot) {

  uint32_t xfer = 0;
  std::string fname;
  apache::thrift::protocol::TType ftype;
  int16_t fid;

  ::apache::thrift::reflection::Schema * schema = iprot->getSchema();
  if (schema != nullptr) {
     ::module_reflection_::reflectionInitializer_9022508676980868684(*schema);
    iprot->setNextStructType(Internship::_reflection_id);
  }
  xfer += iprot->readStructBegin(fname);

  using apache::thrift::protocol::TProtocolException;


  bool isset_weeks = false;

  while (true)
  {
    xfer += iprot->readFieldBegin(fname, ftype, fid);
    if (ftype == apache::thrift::protocol::T_STOP) {
      break;
    }
    switch (fid)
    {
      case 1:
        if (ftype == apache::thrift::protocol::T_I32) {
          xfer += iprot->readI32(this->weeks);
          isset_weeks = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      case 2:
        if (ftype == apache::thrift::protocol::T_STRING) {
          xfer += iprot->readString(this->title);
          this->__isset.title = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      case 3:
        if (ftype == apache::thrift::protocol::T_I32) {
          int32_t ecast1;
          xfer += iprot->readI32(ecast1);
          this->employer = (Company)ecast1;
          this->__isset.employer = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      default:
        xfer += iprot->skip(ftype);
        break;
    }
    xfer += iprot->readFieldEnd();
  }

  xfer += iprot->readStructEnd();

  if (!isset_weeks)
    throw TProtocolException(TProtocolException::MISSING_REQUIRED_FIELD, "Required field 'weeks' was not found in serialized data! Struct: Internship");
  return xfer;
}

void Internship::__clear() {
  weeks = 0;
  title = "";
  employer = static_cast<Company>(0);
  __isset.__clear();
}
uint32_t Internship::write(apache::thrift::protocol::TProtocol* oprot) const {
  uint32_t xfer = 0;
  xfer += oprot->writeStructBegin("Internship");
  xfer += oprot->writeFieldBegin("weeks", apache::thrift::protocol::T_I32, 1);
  xfer += oprot->writeI32(this->weeks);
  xfer += oprot->writeFieldEnd();
  xfer += oprot->writeFieldBegin("title", apache::thrift::protocol::T_STRING, 2);
  xfer += oprot->writeString(this->title);
  xfer += oprot->writeFieldEnd();
  if (this->__isset.employer) {
    xfer += oprot->writeFieldBegin("employer", apache::thrift::protocol::T_I32, 3);
    xfer += oprot->writeI32((int32_t)this->employer);
    xfer += oprot->writeFieldEnd();
  }
  xfer += oprot->writeFieldStop();
  xfer += oprot->writeStructEnd();
  return xfer;
}

void swap(Internship &a, Internship &b) {
  using ::std::swap;
  (void)a;
  (void)b;
  swap(a.weeks, b.weeks);
  swap(a.title, b.title);
  swap(a.employer, b.employer);
  swap(a.__isset, b.__isset);
}

void merge(const Internship& from, Internship& to) {
  using apache::thrift::merge;
  merge(from.weeks, to.weeks);
  merge(from.title, to.title);
  to.__isset.title = to.__isset.title || from.__isset.title;
  if (from.__isset.employer) {
    merge(from.employer, to.employer);
    to.__isset.employer = true;
  }
}

void merge(Internship&& from, Internship& to) {
  using apache::thrift::merge;
  merge(std::move(from.weeks), to.weeks);
  merge(std::move(from.title), to.title);
  to.__isset.title = to.__isset.title || from.__isset.title;
  if (from.__isset.employer) {
    merge(std::move(from.employer), to.employer);
    to.__isset.employer = true;
  }
}

const uint64_t UnEnumStruct::_reflection_id;
void UnEnumStruct::_reflection_register(::apache::thrift::reflection::Schema& schema) {
   ::module_reflection_::reflectionInitializer_18314195816413397484(schema);
}

bool UnEnumStruct::operator == (const UnEnumStruct & rhs) const {
  if (!(this->city == rhs.city))
    return false;
  return true;
}

uint32_t UnEnumStruct::read(apache::thrift::protocol::TProtocol* iprot) {

  uint32_t xfer = 0;
  std::string fname;
  apache::thrift::protocol::TType ftype;
  int16_t fid;

  ::apache::thrift::reflection::Schema * schema = iprot->getSchema();
  if (schema != nullptr) {
     ::module_reflection_::reflectionInitializer_18314195816413397484(*schema);
    iprot->setNextStructType(UnEnumStruct::_reflection_id);
  }
  xfer += iprot->readStructBegin(fname);

  using apache::thrift::protocol::TProtocolException;



  while (true)
  {
    xfer += iprot->readFieldBegin(fname, ftype, fid);
    if (ftype == apache::thrift::protocol::T_STOP) {
      break;
    }
    switch (fid)
    {
      case 1:
        if (ftype == apache::thrift::protocol::T_I32) {
          int32_t ecast3;
          xfer += iprot->readI32(ecast3);
          this->city = (City)ecast3;
          this->__isset.city = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      default:
        xfer += iprot->skip(ftype);
        break;
    }
    xfer += iprot->readFieldEnd();
  }

  xfer += iprot->readStructEnd();

  return xfer;
}

void UnEnumStruct::__clear() {
  city = City(-1);
  __isset.__clear();
}
uint32_t UnEnumStruct::write(apache::thrift::protocol::TProtocol* oprot) const {
  uint32_t xfer = 0;
  xfer += oprot->writeStructBegin("UnEnumStruct");
  xfer += oprot->writeFieldBegin("city", apache::thrift::protocol::T_I32, 1);
  xfer += oprot->writeI32((int32_t)this->city);
  xfer += oprot->writeFieldEnd();
  xfer += oprot->writeFieldStop();
  xfer += oprot->writeStructEnd();
  return xfer;
}

void swap(UnEnumStruct &a, UnEnumStruct &b) {
  using ::std::swap;
  (void)a;
  (void)b;
  swap(a.city, b.city);
  swap(a.__isset, b.__isset);
}

void merge(const UnEnumStruct& from, UnEnumStruct& to) {
  using apache::thrift::merge;
  merge(from.city, to.city);
  to.__isset.city = to.__isset.city || from.__isset.city;
}

void merge(UnEnumStruct&& from, UnEnumStruct& to) {
  using apache::thrift::merge;
  merge(std::move(from.city), to.city);
  to.__isset.city = to.__isset.city || from.__isset.city;
}

const uint64_t Range::_reflection_id;
void Range::_reflection_register(::apache::thrift::reflection::Schema& schema) {
   ::module_reflection_::reflectionInitializer_7757081658652615948(schema);
}

bool Range::operator == (const Range & rhs) const {
  if (!(this->min == rhs.min))
    return false;
  if (!(this->max == rhs.max))
    return false;
  return true;
}

uint32_t Range::read(apache::thrift::protocol::TProtocol* iprot) {

  uint32_t xfer = 0;
  std::string fname;
  apache::thrift::protocol::TType ftype;
  int16_t fid;

  ::apache::thrift::reflection::Schema * schema = iprot->getSchema();
  if (schema != nullptr) {
     ::module_reflection_::reflectionInitializer_7757081658652615948(*schema);
    iprot->setNextStructType(Range::_reflection_id);
  }
  xfer += iprot->readStructBegin(fname);

  using apache::thrift::protocol::TProtocolException;


  bool isset_min = false;
  bool isset_max = false;

  while (true)
  {
    xfer += iprot->readFieldBegin(fname, ftype, fid);
    if (ftype == apache::thrift::protocol::T_STOP) {
      break;
    }
    switch (fid)
    {
      case 1:
        if (ftype == apache::thrift::protocol::T_I32) {
          xfer += iprot->readI32(this->min);
          isset_min = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      case 2:
        if (ftype == apache::thrift::protocol::T_I32) {
          xfer += iprot->readI32(this->max);
          isset_max = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      default:
        xfer += iprot->skip(ftype);
        break;
    }
    xfer += iprot->readFieldEnd();
  }

  xfer += iprot->readStructEnd();

  if (!isset_min)
    throw TProtocolException(TProtocolException::MISSING_REQUIRED_FIELD, "Required field 'min' was not found in serialized data! Struct: Range");
  if (!isset_max)
    throw TProtocolException(TProtocolException::MISSING_REQUIRED_FIELD, "Required field 'max' was not found in serialized data! Struct: Range");
  return xfer;
}

void Range::__clear() {
  min = 0;
  max = 0;
}
uint32_t Range::write(apache::thrift::protocol::TProtocol* oprot) const {
  uint32_t xfer = 0;
  xfer += oprot->writeStructBegin("Range");
  xfer += oprot->writeFieldBegin("min", apache::thrift::protocol::T_I32, 1);
  xfer += oprot->writeI32(this->min);
  xfer += oprot->writeFieldEnd();
  xfer += oprot->writeFieldBegin("max", apache::thrift::protocol::T_I32, 2);
  xfer += oprot->writeI32(this->max);
  xfer += oprot->writeFieldEnd();
  xfer += oprot->writeFieldStop();
  xfer += oprot->writeStructEnd();
  return xfer;
}

void swap(Range &a, Range &b) {
  using ::std::swap;
  (void)a;
  (void)b;
  swap(a.min, b.min);
  swap(a.max, b.max);
}

void merge(const Range& from, Range& to) {
  using apache::thrift::merge;
  merge(from.min, to.min);
  merge(from.max, to.max);
}

void merge(Range&& from, Range& to) {
  using apache::thrift::merge;
  merge(std::move(from.min), to.min);
  merge(std::move(from.max), to.max);
}

