/* DexBuilder
 * Copyright (C) 2021 LSPosed
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
/*
 * Copyright (C) 2018 The Android Open Source Project
 * Modifications copyright (C) 2021 LSPosed Developers
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "dex_builder.h"
#include "slicer/dex_bytecode.h"
#include "slicer/dex_format.h"
#include "slicer/dex_ir.h"

#include <memory>

namespace startop {
namespace dex {

using std::string;

using ::dex::kAccPublic;

const TypeDescriptor TypeDescriptor::Int{"I"};
const TypeDescriptor TypeDescriptor::Void{"V"};
const TypeDescriptor TypeDescriptor::Boolean{"Z"};
const TypeDescriptor TypeDescriptor::Byte{"B"};
const TypeDescriptor TypeDescriptor::Char{"C"};
const TypeDescriptor TypeDescriptor::Double{"D", true};
const TypeDescriptor TypeDescriptor::Float{"F"};
const TypeDescriptor TypeDescriptor::Long{"J", true};
const TypeDescriptor TypeDescriptor::Short{"S"};

const TypeDescriptor TypeDescriptor::Object{"Ljava/lang/Object;"};
const TypeDescriptor TypeDescriptor::String{"Ljava/lang/String;"};
const TypeDescriptor TypeDescriptor::ObjectInt{"Ljava/lang/Integer;"};
const TypeDescriptor TypeDescriptor::ObjectBoolean{"Ljava/lang/Boolean;"};
const TypeDescriptor TypeDescriptor::ObjectByte{"Ljava/lang/Byte;"};
const TypeDescriptor TypeDescriptor::ObjectChar{"Ljava/lang/Character;"};
const TypeDescriptor TypeDescriptor::ObjectDouble{"Ljava/lang/Double;"};
const TypeDescriptor TypeDescriptor::ObjectFloat{"Ljava/lang/Float;"};
const TypeDescriptor TypeDescriptor::ObjectLong{"Ljava/lang/Long;"};
const TypeDescriptor TypeDescriptor::ObjectShort{"Ljava/lang/Short;"};

const phmap::flat_hash_map<TypeDescriptor, TypeDescriptor>
    TypeDescriptor::unbox_map{
        {ObjectInt, Int},   {ObjectBoolean, Boolean}, {ObjectByte, Byte},
        {ObjectChar, Char}, {ObjectDouble, Double},   {ObjectFloat, Float},
        {ObjectLong, Long}, {ObjectShort, Short},
    };

const phmap::flat_hash_map<TypeDescriptor, std::string> value_method_map{
    {TypeDescriptor::ObjectInt, "intValue"},
    {TypeDescriptor::ObjectBoolean, "booleanValue"},
    {TypeDescriptor::ObjectByte, "byteValue"},
    {TypeDescriptor::ObjectChar, "charValue"},
    {TypeDescriptor::ObjectDouble, "doubleValue"},
    {TypeDescriptor::ObjectFloat, "floatValue"},
    {TypeDescriptor::ObjectLong, "longValue"},
    {TypeDescriptor::ObjectShort, "shortValue"},
};
namespace {
// From https://source.android.com/devices/tech/dalvik/dex-format#dex-file-magic
constexpr uint8_t kDexFileMagic[]{0x64, 0x65, 0x78, 0x0a,
                                  0x30, 0x33, 0x35, 0x00};

// Strings lengths can be 32 bits long, but encoded as LEB128 this can take up
// to five bytes.
constexpr size_t kMaxEncodedStringLength{5};

std::string DotToDescriptor(const char *class_name) {
  std::string descriptor(class_name);
  std::replace(descriptor.begin(), descriptor.end(), '.', '/');
  if (descriptor.length() > 0 && descriptor[0] != '[') {
    descriptor = "L" + descriptor + ";";
  }
  return descriptor;
}

} // namespace

void *TrackingAllocator::Allocate(size_t size) {
  std::unique_ptr<uint8_t[]> buffer = std::make_unique<uint8_t[]>(size);
  void *raw_buffer = buffer.get();
  allocations_[raw_buffer] = std::move(buffer);
  return raw_buffer;
}

void TrackingAllocator::Free(void *ptr) {
  allocations_.erase(allocations_.find(ptr));
}

// Write out a DEX file that is basically:
//
// package dextest;
// public class DexTest {
//     public static int foo(String s) { return s.length(); }
// }

TypeDescriptor TypeDescriptor::FromClassname(const std::string &name) {
  return TypeDescriptor{DotToDescriptor(name.c_str())};
}

TypeDescriptor TypeDescriptor::ToBoxType() const {
  assert(is_primitive());
  switch (descriptor_[0]) {
  case 'I':
    return ObjectInt;
  case 'Z':
    return ObjectBoolean;
  case 'C':
    return ObjectChar;
  case 'J':
    return ObjectLong;
  case 'S':
    return ObjectShort;
  case 'F':
    return ObjectFloat;
  case 'D':
    return ObjectDouble;
  case 'B':
    return ObjectByte;
  default:
    assert(false);
    return Object;
  }
}

TypeDescriptor TypeDescriptor::ToUnBoxType() const {
  assert(is_object());
  auto unbox_type_iter = unbox_map.find(*this);
  assert(unbox_type_iter != unbox_map.end());
  return unbox_type_iter->second;
}
char TypeDescriptor::short_descriptor() const {
  if (descriptor_[0] == '[')
    return 'L';
  else
    return descriptor_[0];
}

TypeDescriptor TypeDescriptor::FromDescriptor(const char descriptor) {
  switch (descriptor) {
    case 'I':
      return Int;
    case 'Z':
      return Boolean;
    case 'C':
      return Char;
    case 'J':
      return Long;
    case 'S':
      return Short;
    case 'F':
      return Float;
    case 'D':
      return Double;
    case 'B':
      return Byte;
    case 'V':
      return Void;
    default:
      return Object;
  }
}

TypeDescriptor TypeDescriptor::FromDescriptor(const string &descriptor) {
  switch (descriptor[0]) {
  case 'I':
    return Int;
  case 'Z':
    return Boolean;
  case 'C':
    return Char;
  case 'J':
    return Long;
  case 'S':
    return Short;
  case 'F':
    return Float;
  case 'D':
    return Double;
  case 'B':
    return Byte;
  case 'V':
    return Void;
  default:
    return TypeDescriptor{descriptor, false};
  }
}

DexBuilder::DexBuilder() : dex_file_{std::make_shared<ir::DexFile>()} {
  dex_file_->magic = slicer::MemView{kDexFileMagic, sizeof(kDexFileMagic)};
}

slicer::MemView DexBuilder::CreateImage(bool checksum) {
  ::dex::Writer writer(dex_file_);
  size_t image_size{0};
  ::dex::u1 *image = writer.CreateImage(&allocator_, &image_size, checksum);
  return slicer::MemView{image, image_size};
}

ir::String *DexBuilder::GetOrAddString(const std::string &string) {
  auto it = strings_.find(string);
  if (it == strings_.end()) {
    // Need to encode the length and then write out the bytes, including 1 byte
    // for null terminator
    auto buffer = std::make_unique<uint8_t[]>(string.size() +
                                              kMaxEncodedStringLength + 1);
    size_t actual_len = 0u;
    const char* s = string.data();
    while (*s) actual_len += (*s++ & 0xc0) != 0x80;
    uint8_t *string_data_start =
        ::dex::WriteULeb128(buffer.get(), actual_len);

    size_t header_length = reinterpret_cast<uintptr_t>(string_data_start) -
                           reinterpret_cast<uintptr_t>(buffer.get());

    auto end = std::copy(string.begin(), string.end(), string_data_start);
    *end = '\0';

    auto* entry = Alloc<ir::String>();
    // +1 for null terminator
    entry->data =
        slicer::MemView{buffer.get(), header_length + string.size() + 1};
    it = strings_.emplace(entry->c_str(), entry).first;
    ::dex::u4 const new_index = dex_file_->strings_indexes.AllocateIndex();
    dex_file_->strings_map[new_index] = entry;
    entry->orig_index = new_index;
    string_data_.push_back(std::move(buffer));
  }
  return it->second;
}

ClassBuilder DexBuilder::MakeClass(const std::string &name) {
  auto *class_def = Alloc<ir::Class>();
  ir::Type *type_def = GetOrAddType(DotToDescriptor(name.c_str()));
  type_def->class_def = class_def;

  class_def->type = type_def;
  class_def->super_class = GetOrAddType(TypeDescriptor::Object);
  class_def->access_flags = kAccPublic;
  return ClassBuilder{this, name, class_def};
}

ir::Type *DexBuilder::GetOrAddType(const std::string &descriptor) {
  if (auto type = types_by_descriptor_.find(descriptor);
      type != types_by_descriptor_.end()) {
    return type->second;
  }

  ir::Type *type = Alloc<ir::Type>();
  type->descriptor = GetOrAddString(descriptor);
  types_by_descriptor_[type->descriptor->c_str()] = type;
  type->orig_index = dex_file_->types_indexes.AllocateIndex();
  dex_file_->types_map[type->orig_index] = type;
  return type;
}

ir::FieldDecl *DexBuilder::GetOrAddField(TypeDescriptor parent,
                                         const std::string &name,
                                         TypeDescriptor type) {
  const auto key = std::make_tuple(parent, name);
  if (auto field = field_decls_by_key_.find(key);
      field != field_decls_by_key_.end()) {
    return field->second;
  }

  ir::FieldDecl *field = Alloc<ir::FieldDecl>();
  field->parent = GetOrAddType(parent);
  field->name = GetOrAddString(name);
  field->type = GetOrAddType(type);
  field->orig_index = dex_file_->fields_indexes.AllocateIndex();
  dex_file_->fields_map[field->orig_index] = field;
  field_decls_by_key_[key] = field;
  return field;
}

ir::Proto *Prototype::Encode(DexBuilder *dex) const {
  auto *proto = dex->Alloc<ir::Proto>();
  proto->shorty = dex->GetOrAddString(Shorty());
  proto->return_type = dex->GetOrAddType(return_type_.descriptor());
  if (param_types_.size() > 0) {
    proto->param_types = dex->Alloc<ir::TypeList>();
    for (const auto &param_type : param_types_) {
      proto->param_types->types.push_back(
          dex->GetOrAddType(param_type.descriptor()));
    }
  } else {
    proto->param_types = nullptr;
  }
  return proto;
}

std::string Prototype::Shorty() const {
  std::string shorty;
  shorty += return_type_.short_descriptor();
  for (const auto &type_descriptor : param_types_) {
    shorty += type_descriptor.short_descriptor();
  }
  return shorty;
}

const TypeDescriptor &Prototype::ArgType(size_t index) const {
  assert(index < param_types_.size());
  return param_types_[index];
}

ClassBuilder::ClassBuilder(DexBuilder *parent, const std::string &name,
                           ir::Class *class_def)
    : parent_(parent), type_descriptor_{TypeDescriptor::FromClassname(name)},
      class_(class_def) {}

MethodBuilder ClassBuilder::CreateMethod(const std::string &name,
                                         const Prototype &prototype) {
  ir::MethodDecl *decl =
      parent_->GetOrDeclareMethod(type_descriptor_, name, prototype).decl;
  return {this, class_, decl};
}

FieldBuilder ClassBuilder::CreateField(const std::string &name,
                                       const TypeDescriptor &type) {
  ir::FieldDecl *decl = parent_->GetOrAddField(type_descriptor_, name, type);
  return {this, class_, decl};
}

ClassBuilder ClassBuilder::setSuperClass(const TypeDescriptor &type) {
    class_->super_class = parent_->GetOrAddType(type);
    return *this;
}

void ClassBuilder::set_source_file(const string &source) {
  class_->source_file = parent_->GetOrAddString(source);
}

FieldBuilder::FieldBuilder(ClassBuilder *parent, ir::Class *class_def,
                           ir::FieldDecl *decl)
    : parent_(parent), class_(class_def), decl_(decl) {}

ir::EncodedField *FieldBuilder::Encode() {
  auto *field = dex_file()->Alloc<ir::EncodedField>();
  field->decl = decl_;
  field->access_flags = access_flags_;
  class_->static_fields.push_back(field);
  return field;
}

MethodBuilder::MethodBuilder(ClassBuilder *parent, ir::Class *class_def,
                             ir::MethodDecl *decl)
    : parent_{parent}, class_{class_def}, decl_{decl} {}

ir::EncodedMethod *MethodBuilder::Encode() {
  auto *method = dex_file()->Alloc<ir::EncodedMethod>();
  method->decl = decl_;

  method->access_flags = access_flags_;

  auto *code = dex_file()->Alloc<ir::Code>();
  assert(decl_->prototype != nullptr);

  size_t num_args = 0;
  if (decl_->prototype->param_types != nullptr) {
    for (auto &type : decl_->prototype->param_types->types) {
      if (type->GetCategory() == ir::Type::Category::WideScalar) {
        num_args += 1;
      }
      num_args += 1;
    }
  }
  code->registers = NumRegisters() + num_args + kMaxScratchRegisters;
  code->ins_count = num_args;
  EncodeInstructions();
  code->instructions =
      slicer::ArrayView<const ::dex::u2>(buffer_.data(), buffer_.size());
  size_t const return_count =
      decl_->prototype->return_type ==
              dex_file()->GetOrAddType(TypeDescriptor::Void)
          ? 0
          : 1;
  code->outs_count = std::max(return_count, max_args_);
  method->code = code;

  class_->direct_methods.push_back(method);

  return method;
}

LiveRegister MethodBuilder::AllocRegister() {
  // Find a free register
  for (size_t i = 0; i < register_liveness_.size(); ++i) {
    if (!register_liveness_[i]) {
      register_liveness_[i] = true;
      return LiveRegister{&register_liveness_, i};
    }
  }

  // If we get here, all the registers are in use, so we have to allocate a new
  // one.
  register_liveness_.push_back(true);
  return LiveRegister{&register_liveness_, register_liveness_.size() - 1};
}

Value MethodBuilder::MakeLabel() {
  labels_.push_back({});
  return Value::Label(labels_.size() - 1);
}

MethodBuilder &MethodBuilder::AddInstruction(Instruction instruction) {
  instructions_.push_back(instruction);
  return *this;
}
MethodBuilder &MethodBuilder::BuildBoxIfPrimitive(const Value &target,
                                                  const TypeDescriptor &type,
                                                  const Value &src) {
  if (type.is_primitive()) {
    auto box_type{type.ToBoxType()};
    MethodDeclData value_of{dex_file()->GetOrDeclareMethod(
        box_type, "valueOf", Prototype{box_type, type})};
    if (type.is_wide()) {
      auto wide_pair = src.WidePair();
      if (RegisterValue(src) >= 16 || RegisterValue(wide_pair) >= 16) {
        AddInstruction(Instruction::InvokeStaticObjectRange(value_of.id, target, src, 2));
      } else {
        AddInstruction(Instruction::InvokeStaticObject(value_of.id, target, src,
                                                       src.WidePair()));
      }
    } else {
        if(RegisterValue(src) >= 16) {
            AddInstruction(Instruction::InvokeStaticObjectRange(value_of.id, target, src, 1));
        } else {
            AddInstruction(Instruction::InvokeStaticObject(value_of.id, target, src));
        }
    }
  } else if (target != src) {
    AddInstruction(Instruction::OpWithArgs(Op::kMoveObject, target, src));
  }
  return *this;
}

MethodBuilder &MethodBuilder::BuildUnBoxIfPrimitive(const Value &target,
                                                    const TypeDescriptor &type,
                                                    const Value &src) {
  if (type.is_object()) {
    auto unbox_type{type.ToUnBoxType()};
    MethodDeclData value{dex_file()->GetOrDeclareMethod(
        type, value_method_map.at(type), Prototype{unbox_type})};
    if (unbox_type.is_wide())
      AddInstruction(Instruction::InvokeVirtualWide(value.id, target, src));
    else
      AddInstruction(Instruction::InvokeVirtual(value.id, target, src));
  } else if (target != src) {
    AddInstruction(Instruction::OpWithArgs(Op::kMove, target, src));
  }
  return *this;
}

void MethodBuilder::EncodeInstruction(const Instruction &instruction) {
  switch (instruction.opcode()) {
  case Instruction::Op::kReturn:
    return EncodeReturn(instruction, ::dex::Opcode::OP_RETURN);
  case Instruction::Op::kReturnObject:
    return EncodeReturn(instruction, ::dex::Opcode::OP_RETURN_OBJECT);
  case Instruction::Op::kReturnWide:
    return EncodeReturn(instruction, ::dex::Opcode::OP_RETURN_WIDE);
  case Instruction::Op::kMove:
  case Instruction::Op::kMoveObject:
  case Instruction::Op::kMoveWide:
    return EncodeMove(instruction);
  case Instruction::Op::kInvokeVirtual:
    return EncodeInvoke(instruction, ::dex::Opcode::OP_INVOKE_VIRTUAL);
  case Instruction::Op::kInvokeDirect:
    return EncodeInvoke(instruction, ::dex::Opcode::OP_INVOKE_DIRECT);
  case Instruction::Op::kInvokeStatic:
    return EncodeInvoke(instruction, ::dex::Opcode::OP_INVOKE_STATIC);
  case Instruction::Op::kInvokeInterface:
    return EncodeInvoke(instruction, ::dex::Opcode::OP_INVOKE_INTERFACE);
  case Instruction::Op::kInvokeVirtualRange:
    return EncodeInvokeRange(instruction, ::dex::Opcode::OP_INVOKE_VIRTUAL_RANGE);
  case Instruction::Op::kInvokeDirectRange:
    return EncodeInvokeRange(instruction, ::dex::Opcode::OP_INVOKE_DIRECT_RANGE);
  case Instruction::Op::kInvokeStaticRange:
    return EncodeInvokeRange(instruction, ::dex::Opcode::OP_INVOKE_STATIC_RANGE);
  case Instruction::Op::kInvokeInterfaceRange:
    return EncodeInvokeRange(instruction, ::dex::Opcode::OP_INVOKE_INTERFACE_RANGE);
  case Instruction::Op::kBindLabel:
    return BindLabel(instruction.args()[0]);
  case Instruction::Op::kBranchEqz:
    return EncodeBranch(::dex::Opcode::OP_IF_EQZ, instruction);
  case Instruction::Op::kBranchNEqz:
    return EncodeBranch(::dex::Opcode::OP_IF_NEZ, instruction);
  case Instruction::Op::kNew:
    return EncodeNew(instruction);
  case Instruction::Op::kNewArray:
    return EncodeNewArray(instruction);
  case Instruction::Op::kCheckCast:
    return EncodeCast(instruction);
  case Instruction::Op::kGetStaticField:
  case Instruction::Op::kGetStaticObjectField:
  case Instruction::Op::kSetStaticField:
  case Instruction::Op::kSetStaticObjectField:
  case Instruction::Op::kGetInstanceField:
  case Instruction::Op::kSetInstanceField:
    return EncodeFieldOp(instruction);
  case Instruction::Op::kAputObject:
    return EncodeAput(instruction);
  }
}

void MethodBuilder::EncodeReturn(const Instruction &instruction,
                                 ::dex::Opcode opcode) {
  assert(!instruction.dest().has_value());
  if (instruction.args().size() == 0) {
    Encode10x(::dex::Opcode::OP_RETURN_VOID);
  } else {
    assert(1 == instruction.args().size());
    size_t source = RegisterValue(instruction.args()[0]);
    Encode11x(opcode, source);
  }
}

void MethodBuilder::EncodeMove(const Instruction &instruction) {
  assert(Instruction::Op::kMove == instruction.opcode() ||
         Instruction::Op::kMoveObject == instruction.opcode() ||
         Instruction::Op::kMoveWide == instruction.opcode());
  assert(instruction.dest().has_value());
  assert(instruction.dest()->is_variable());
  assert(1 == instruction.args().size());

  const Value &source = instruction.args()[0];

  if (source.is_immediate() && Instruction::Op::kMove == instruction.opcode()) {
    if (RegisterValue(*instruction.dest()) < 16 && source.value() < 8) {
      Encode11n(::dex::Opcode::OP_CONST_4, RegisterValue(*instruction.dest()),
                source.value());
    } else if (source.value() <= 65535) {
      Encode21s(::dex::Opcode::OP_CONST_16, RegisterValue(*instruction.dest()),
                source.value());
    } else {
      Encode31i(::dex::Opcode::OP_CONST, RegisterValue(*instruction.dest()),
                source.value());
    }
  } else if (source.is_immediate() &&
             Instruction::Op::kMoveWide == instruction.opcode()) {
    if (source.value() <= 65535) {
      Encode21s(::dex::Opcode::OP_CONST_WIDE_16,
                RegisterValue(*instruction.dest()), source.value());
    } else if (source.value() <= 4294967295) {
      Encode31i(::dex::Opcode::OP_CONST_WIDE_32,
                RegisterValue(*instruction.dest()), source.value());
    } else {
      assert(false && "not supported yet");
      // Encode51i(::dex::Opcode::OP_CONST_WIDE,
      // RegisterValue(*instruction.dest()), source.value());
    }
  } else if (source.is_string()) {
    assert(RegisterValue(*instruction.dest()) < 256);
    assert(source.value() < 65536); // make sure we don't need a jumbo string
    Encode21c(::dex::Opcode::OP_CONST_STRING,
              RegisterValue(*instruction.dest()), source.value());
  } else if (source.is_variable()) {
    // For the moment, we only use this when we need to reshuffle registers for
    // an invoke instruction, meaning we are too big for the 4-bit version.
    // We'll err on the side of caution and always generate the 16-bit form of
    // the instruction.
    auto opcode = instruction.opcode() == Instruction::Op::kMove
                      ? ::dex::Opcode::OP_MOVE_16
                      : (instruction.opcode() == Instruction::Op::kMoveWide
                             ? ::dex::Opcode::OP_MOVE_WIDE_16
                             : ::dex::Opcode::OP_MOVE_OBJECT_16);
    Encode32x(opcode, RegisterValue(*instruction.dest()),
              RegisterValue(source));
  } else {
    assert(false);
  }
}

void MethodBuilder::EncodeInvoke(const Instruction &instruction,
                                 ::dex::Opcode opcode) {
  constexpr size_t kMaxArgs = 5;

  // Currently, we only support up to 5 arguments.
  assert(instruction.args().size() < kMaxArgs);

  uint8_t arguments[kMaxArgs]{0};
  bool has_long_args = false;
  for (size_t i = 0; i < instruction.args().size(); ++i) {
    assert(instruction.args()[i].is_variable());
    arguments[i] = RegisterValue(instruction.args()[i]);
    if (!IsShortRegister(arguments[i])) {
      has_long_args = true;
    }
  }

  if (has_long_args) {
    assert(false && "long args should use invoke range");
  } else {
    Encode35c(opcode, instruction.args().size(), instruction.index_argument(),
              arguments[0], arguments[1], arguments[2], arguments[3],
              arguments[4]);
  }

  // If there is a return value, add a move-result instruction
  if (instruction.dest().has_value()) {
    Encode11x(instruction.result_is_object()
                  ? ::dex::Opcode::OP_MOVE_RESULT_OBJECT
                  : (instruction.result_is_wide()
                         ? ::dex::Opcode::OP_MOVE_RESULT_WIDE
                         : ::dex::Opcode::OP_MOVE_RESULT),
              RegisterValue(*instruction.dest()));
  }
  max_args_ = std::max(max_args_, instruction.args().size());
}

void MethodBuilder::EncodeInvokeRange(const Instruction &instruction,
                                      ::dex::Opcode opcode) {
  const auto &args = instruction.args();
  assert(args.size() == 2);
  assert(args[1].is_immediate());
  Encode3rc(opcode, args[1].value(),
            instruction.index_argument(), RegisterValue(args[0]));
  // If there is a return value, add a move-result instruction
  if (instruction.dest().has_value()) {
    Encode11x(instruction.result_is_object()
                  ? ::dex::Opcode::OP_MOVE_RESULT_OBJECT
                  : (instruction.result_is_wide()
                         ? ::dex::Opcode::OP_MOVE_RESULT_WIDE
                         : ::dex::Opcode::OP_MOVE_RESULT),
              RegisterValue(*instruction.dest()));
  }
  max_args_ = std::max(max_args_, instruction.args().size());
}

// Encodes a conditional branch that tests a single argument.
void MethodBuilder::EncodeBranch(::dex::Opcode op,
                                 const Instruction &instruction) {
  const auto &args = instruction.args();
  const auto &test_value = args[0];
  const auto &branch_target = args[1];
  assert(2 == args.size());
  assert(test_value.is_variable());
  assert(branch_target.is_label());

  size_t instruction_offset = buffer_.size();
  size_t field_offset = buffer_.size() + 1;
  Encode21c(op, RegisterValue(test_value),
            LabelValue(branch_target, instruction_offset, field_offset));
}

void MethodBuilder::EncodeNew(const Instruction &instruction) {
  assert(Instruction::Op::kNew == instruction.opcode());
  assert(instruction.dest().has_value());
  assert(instruction.dest()->is_variable());
  assert(1 == instruction.args().size());

  const Value &type = instruction.args()[0];
  assert(RegisterValue(*instruction.dest()) < 256);
  assert(type.is_type());
  Encode21c(::dex::Opcode::OP_NEW_INSTANCE, RegisterValue(*instruction.dest()),
            type.value());
}

void MethodBuilder::EncodeCast(const Instruction &instruction) {
  assert(Instruction::Op::kCheckCast == instruction.opcode());
  assert(instruction.dest().has_value());
  assert(instruction.dest()->is_variable());
  assert(1 == instruction.args().size());

  const Value &type = instruction.args()[0];
  assert(RegisterValue(*instruction.dest()) < 256);
  assert(type.is_type());
  Encode21c(::dex::Opcode::OP_CHECK_CAST, RegisterValue(*instruction.dest()),
            type.value());
}

void MethodBuilder::EncodeNewArray(const Instruction &instruction) {
  assert(Instruction::Op::kNewArray == instruction.opcode());
  assert(instruction.dest().has_value());
  assert(instruction.dest()->is_variable());
  assert(2 == instruction.args().size());
  const auto &args = instruction.args();
  const Value &type = args[1];
  Encode22c(::dex::Opcode::OP_NEW_ARRAY, RegisterValue(*instruction.dest()),
            RegisterValue(args[0]), type.value());
}

void MethodBuilder::EncodeAput(const Instruction &instruction) {
  assert(Instruction::Op::kAputObject == instruction.opcode());
  assert(instruction.dest().has_value());
  assert(instruction.dest()->is_variable());
  assert(2 == instruction.args().size());
  const auto &args = instruction.args();
  switch (instruction.opcode()) {
  case Instruction::Op::kAputObject: {
    Encode23x(::dex::Opcode::OP_APUT_OBJECT, RegisterValue(*instruction.dest()),
              RegisterValue(args[0]), RegisterValue(args[1]));
    break;
  }
  default: {
    assert(false);
  }
  }
}

void MethodBuilder::EncodeFieldOp(const Instruction &instruction) {
  const auto &args = instruction.args();
  switch (instruction.opcode()) {
  case Instruction::Op::kGetStaticObjectField:
  case Instruction::Op::kGetStaticField: {
    assert(instruction.dest().has_value());
    assert(instruction.dest()->is_variable());
    assert(0 == instruction.args().size());

    Encode21c(instruction.opcode() == Instruction::Op::kGetStaticField
                  ? ::dex::Opcode::OP_SGET
                  : ::dex::Opcode::OP_SGET_OBJECT,
              RegisterValue(*instruction.dest()), instruction.index_argument());
    break;
  }
  case Instruction::Op::kSetStaticObjectField:
  case Instruction::Op::kSetStaticField: {
    assert(!instruction.dest().has_value());
    assert(1 == args.size());
    assert(args[0].is_variable());

    Encode21c(instruction.opcode() == Instruction::Op::kSetStaticField
                  ? ::dex::Opcode::OP_SPUT
                  : ::dex::Opcode::OP_SPUT_OBJECT,
              RegisterValue(args[0]), instruction.index_argument());
    break;
  }
  case Instruction::Op::kGetInstanceField: {
    assert(instruction.dest().has_value());
    assert(instruction.dest()->is_variable());
    assert(1 == instruction.args().size());

    Encode22c(::dex::Opcode::OP_IGET, RegisterValue(*instruction.dest()),
              RegisterValue(args[0]), instruction.index_argument());
    break;
  }
  case Instruction::Op::kSetInstanceField: {
    assert(!instruction.dest().has_value());
    assert(2 == args.size());
    assert(args[0].is_variable());
    assert(args[1].is_variable());

    Encode22c(::dex::Opcode::OP_IPUT, RegisterValue(args[1]),
              RegisterValue(args[0]), instruction.index_argument());
    break;
  }
  default: {
    assert(false);
  }
  }
}

size_t MethodBuilder::RegisterValue(const Value &value) const {
  if (value.is_register()) {
    return value.value();
  } else if (value.is_parameter()) {
    return value.value() + NumRegisters();
  }
  assert(false && "Must be either a parameter or a register");
  return 0;
}

void MethodBuilder::BindLabel(const Value &label_id) {
  assert(label_id.is_label());

  LabelData &label = labels_[label_id.value()];
  assert(!label.bound_address.has_value());

  label.bound_address = buffer_.size();

  // patch any forward references to this label.
  for (const auto &ref : label.references) {
    buffer_[ref.field_offset] = *label.bound_address - ref.instruction_offset;
  }
  // No point keeping these around anymore.
  label.references.clear();
}

::dex::u2 MethodBuilder::LabelValue(const Value &label_id,
                                    size_t instruction_offset,
                                    size_t field_offset) {
  assert(label_id.is_label());
  LabelData &label = labels_[label_id.value()];

  // Short-circuit if the label is already bound.
  if (label.bound_address.has_value()) {
    return *label.bound_address - instruction_offset;
  }

  // Otherwise, save a reference to where we need to back-patch later.
  label.references.push_front(LabelReference{instruction_offset, field_offset});
  return 0;
}

const MethodDeclData &DexBuilder::GetOrDeclareMethod(TypeDescriptor type,
                                                     const std::string &name,
                                                     Prototype prototype) {
  MethodDeclData &entry = method_id_map_[{type, name, prototype}];

  if (entry.decl == nullptr) {
    // This method has not already been declared, so declare it.
    ir::MethodDecl *decl = dex_file_->Alloc<ir::MethodDecl>();
    // The method id is the last added method.
    size_t id = dex_file_->methods.size() - 1;

    ir::String *dex_name{GetOrAddString(name)};
    decl->name = dex_name;
    decl->parent = GetOrAddType(type.descriptor());
    decl->prototype = GetOrEncodeProto(prototype);

    // update the index -> ir node map (see
    // tools/dexter/slicer/dex_ir_builder.cc)
    auto new_index = dex_file_->methods_indexes.AllocateIndex();
    auto &ir_node = dex_file_->methods_map[new_index];
    assert(ir_node == nullptr);
    ir_node = decl;
    decl->orig_index = decl->index = new_index;

    entry = {id, decl};
  }

  return entry;
}

std::optional<const Prototype>
DexBuilder::GetPrototypeByMethodId(size_t method_id) const {
  for (const auto &entry : method_id_map_) {
    if (entry.second.id == method_id) {
      return entry.first.prototype;
    }
  }
  return {};
}

ir::Proto *DexBuilder::GetOrEncodeProto(Prototype prototype) {
  ir::Proto *&ir_proto = proto_map_[prototype];
  if (ir_proto == nullptr) {
    ir_proto = prototype.Encode(this);
  }
  return ir_proto;
}

} // namespace dex
} // 
