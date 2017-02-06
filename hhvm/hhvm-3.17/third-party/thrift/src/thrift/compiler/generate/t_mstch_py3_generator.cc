/*
 * Copyright 2016 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <thrift/compiler/generate/t_mstch_generator.h>

#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

namespace {

class t_mstch_py3_generator : public t_mstch_generator {
  public:
    t_mstch_py3_generator(
        t_program* program,
        const std::map<std::string, std::string>& parsed_options,
        const std::string& /* option_string unused */)
        : t_mstch_generator(program, "py3", parsed_options) {

      this->out_dir_base_ = "gen-py3";
      auto include_prefix = this->get_option("include_prefix");
      if (include_prefix) {
        program->set_include_prefix(include_prefix.value());
      }
    }

    void generate_program() override;
    mstch::map extend_program(const t_program&) const override;
    mstch::map extend_type(const t_type&) const override;

  protected:
    void generate_structs(const t_program&);
    void generate_services(const t_program&);
    mstch::array get_return_types(const t_program&) const;
    mstch::array get_container_types(const t_program&) const;
    string flatten_type_name(const t_type&) const;

  private:
    void load_container_type(
      vector<t_type*>& container_types,
      std::set<string>& visited_names,
      t_type* type
    ) const;
};

mstch::map t_mstch_py3_generator::extend_program(const t_program& program) const {
  auto cpp_namespace = program.get_namespace("cpp2");
  if (cpp_namespace == "") {
    cpp_namespace = program.get_namespace("cpp");
    if (cpp_namespace == "") {
      cpp_namespace = "cpp2";
    }
    else {
      cpp_namespace = cpp_namespace + "cpp2";
    }
  }
  vector<string> ns;
  boost::algorithm::split(ns, cpp_namespace, boost::algorithm::is_any_of("."));

  mstch::map result {
    {"returnTypes", this->get_return_types(program)},
    {"containerTypes", this->get_container_types(program)},
    {"cppNamespaces", this->dump_elems(ns)},
  };
  return result;
}

mstch::map  t_mstch_py3_generator::extend_type(const t_type& type) const {
  mstch::map result {
    {"flat_name", this->flatten_type_name(type)},
  };
  return result;
}

void t_mstch_py3_generator::generate_structs(const t_program& program) {
  auto basename = program.get_name() + "_types";
  this->render_to_file(program, "Struct.pxd", basename + ".pxd");
  this->render_to_file(program, "Struct.pyx", basename + ".pyx");
}

void t_mstch_py3_generator::generate_services(const t_program& program) {
  auto name = this->get_program()->get_name();
  this->render_to_file(program, "Services.pxd", name + "_services.pxd");
  auto basename = name + "_services_wrapper";
  this->render_to_file(program, "ServicesWrapper.h", basename + ".h");
  this->render_to_file(program, "ServicesWrapper.cpp", basename + ".cpp");
  this->render_to_file(program, "ServicesWrapper.pxd", basename + ".pxd");
  this->render_to_file(program, "CythonServices.pyx", name + "_services.pyx");

}

mstch::array t_mstch_py3_generator::get_return_types(
  const t_program& program
) const {
  mstch::array distinct_return_types;
  std::set<string> visited_names;

  for (const auto service : program.get_services()) {
    for (const auto function : service->get_functions()) {
      const auto returntype = function->get_returntype();
      string flat_name = this->flatten_type_name(*returntype);
      if (!visited_names.count(flat_name)) {
        distinct_return_types.push_back(this->dump(*returntype));
        visited_names.insert(flat_name);
      }
    }
  }
  return distinct_return_types;
}

mstch::array t_mstch_py3_generator::get_container_types(
  const t_program& program
) const {
  vector<t_type*> container_types;
  std::set<string> visited_names;

  for (const auto service : program.get_services()) {
    for (const auto function : service->get_functions()) {
      for (const auto field : function->get_arglist()->get_members()) {
        auto arg_type = field->get_type();
        this->load_container_type(
          container_types,
          visited_names,
          arg_type
        );
      }
      auto return_type = function->get_returntype();
      this->load_container_type(container_types, visited_names, return_type);
    }
  }
  for (const auto object :program.get_objects()) {
    for (const auto field : object->get_members()) {
      auto ref_type = field->get_type();
      this->load_container_type(container_types, visited_names, ref_type);
    }
  }
  return this->dump_elems(container_types);
}

void t_mstch_py3_generator::load_container_type(
  vector<t_type*>& container_types,
  std::set<string>& visited_names,
  t_type* type
) const {
  if (!type->is_container()) return;
  string flat_name = this->flatten_type_name(*type);
  if (visited_names.count(flat_name)) return;

  visited_names.insert(flat_name);
  container_types.push_back(type);
}

string t_mstch_py3_generator::flatten_type_name(const t_type& type) const {
    if (type.is_list()) {
      return "List__" + this->flatten_type_name(
        *dynamic_cast<const t_list&>(type).get_elem_type()
      );
  } else if (type.is_set()) {
    return "Set__" + this->flatten_type_name(
      *dynamic_cast<const t_set&>(type).get_elem_type()
    );
  } else if (type.is_map()) {
      return ("Map__" +
        this->flatten_type_name(
          *dynamic_cast<const t_map&>(type).get_key_type()
        ) + "_" +
        this->flatten_type_name(
          *dynamic_cast<const t_map&>(type).get_val_type()
        )
      );
  } else {
    return type.get_name();
  }
}

void t_mstch_py3_generator::generate_program() {
  mstch::config::escape = [](const std::string& s) { return s; };
  this->generate_structs(*this->get_program());
  this->generate_services(*this->get_program());
}

THRIFT_REGISTER_GENERATOR(
  mstch_py3,
  "Python 3",
  "    include_prefix:  Use full include paths in generated files.\n"
);
}
