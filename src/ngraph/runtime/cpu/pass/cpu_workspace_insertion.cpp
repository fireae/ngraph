//*****************************************************************************
// Copyright 2017-2018 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//*****************************************************************************

#include "cpu_workspace_insertion.hpp"
#include <algorithm>
#include <iostream>
#include <numeric>
#include <unordered_set>
#include "ngraph/graph_util.hpp"
#include "ngraph/log.hpp"
#include "ngraph/op/add.hpp"
#include "ngraph/op/add.hpp"
#include "ngraph/op/batch_norm.hpp"
#include "ngraph/op/broadcast.hpp"
#include "ngraph/op/broadcast.hpp"
#include "ngraph/op/constant.hpp"
#include "ngraph/op/convolution.hpp"
#include "ngraph/op/divide.hpp"
#include "ngraph/op/dot.hpp"
#include "ngraph/op/exp.hpp"
#include "ngraph/op/get_output_element.hpp"
#include "ngraph/op/max_pool.hpp"
#include "ngraph/op/multiply.hpp"
#include "ngraph/op/negative.hpp"
#include "ngraph/op/pad.hpp"
#include "ngraph/op/parameter.hpp"
#include "ngraph/op/relu.hpp"
#include "ngraph/op/reshape.hpp"
#include "ngraph/op/sqrt.hpp"
#include "ngraph/op/subtract.hpp"
#include "ngraph/op/sum.hpp"
#include "ngraph/pattern/matcher.hpp"
#include "ngraph/pattern/op/label.hpp"
#include "ngraph/pattern/op/skip.hpp"
#include "ngraph/runtime/cpu/op/batch_norm_relu.hpp"
#include "ngraph/runtime/cpu/op/conv_bias.hpp"
#include "ngraph/runtime/cpu/op/conv_relu.hpp"
#include "ngraph/runtime/cpu/op/matmul_bias.hpp"
#include "ngraph/runtime/cpu/op/max_pool_with_indices.hpp"
#include "ngraph/runtime/cpu/op/sigmoid.hpp"

using namespace ngraph;

static std::shared_ptr<pattern::Matcher> create_maxpool_with_indices_matcher()
{
    Shape shape_data{1, 1, 14};
    auto data = std::make_shared<pattern::op::Label>(element::f32, shape_data);
    Shape window_shape{3};
    auto max_pool = std::make_shared<op::MaxPool>(data, window_shape);
    auto delta = std::make_shared<pattern::op::Label>(element::f32, max_pool->get_shape());
    auto is_max_pool = pattern::has_class<op::MaxPool>();
    auto max_pool_label =
        std::make_shared<pattern::op::Label>(element::f32, max_pool->get_shape(), is_max_pool);
    auto max_pool_bprop =
        std::make_shared<op::MaxPoolBackprop>(data,
                                              delta,
                                              max_pool_label,
                                              max_pool->get_window_shape(),
                                              max_pool->get_window_movement_strides(),
                                              max_pool->get_padding_below(),
                                              max_pool->get_padding_above());
    return std::make_shared<pattern::Matcher>(max_pool_bprop);
}

bool runtime::cpu::pass::CPUWorkspaceInsertion::run_on_function(std::shared_ptr<ngraph::Function> f)
{
    auto matcher = create_maxpool_with_indices_matcher();

    bool replaced = false;
    for (auto n : f->get_ordered_ops())
    {
        if (n->is_output() || n->is_parameter())
        {
            continue;
        }

        if (matcher->match(n) && transform(*matcher))
        {
            replaced = true;
        }
    }

    return replaced;
}

bool runtime::cpu::pass::CPUWorkspaceInsertion::transform(pattern::Matcher& m)
{
    auto data = std::static_pointer_cast<pattern::op::Label>(m.get_pattern()->get_argument(0));
    auto delta = std::static_pointer_cast<pattern::op::Label>(m.get_pattern()->get_argument(1));
    auto max_pool = std::static_pointer_cast<pattern::op::Label>(m.get_pattern()->get_argument(2));
    NGRAPH_DEBUG << "In a callback for construct_max_pool_with_indices against "
                 << m.get_match_root()->get_name();

    auto pattern_map = m.get_pattern_map();
    auto m_max_pool = std::static_pointer_cast<op::MaxPool>(pattern_map[max_pool]);
    auto m_max_pool_bprop = std::static_pointer_cast<op::MaxPoolBackprop>(m.get_match_root());

    if (m_max_pool_bprop->get_shape().size() != 4 ||
        m_max_pool_bprop->get_window_shape().size() != 2 ||
        m_max_pool_bprop->get_input_element_type(0) != element::f32)
    {
        NGRAPH_DEBUG << "MKLDNN doesn't support inputs of given shape type";
        return false;
    }

    auto max_pool_with_indices =
        std::make_shared<op::MaxPoolWithIndices>(pattern_map[data],
                                                 m_max_pool->get_window_shape(),
                                                 m_max_pool->get_window_movement_strides(),
                                                 m_max_pool->get_padding_below(),
                                                 m_max_pool->get_padding_above());

    auto max_pool_with_indices_output =
        std::make_shared<op::GetOutputElement>(max_pool_with_indices, 0);
    auto max_pool_with_indices_indices =
        std::make_shared<op::GetOutputElement>(max_pool_with_indices, 1);

    // rewire users to use a new MaxPoolWithIndices (maxpool's output)
    for (auto& o : m_max_pool->get_outputs())
    {
        std::set<ngraph::descriptor::Input*> copy{begin(o.get_inputs()), end(o.get_inputs())};
        for (auto i : copy)
        {
            i->replace_output(max_pool_with_indices_output->get_outputs().at(0));
        }
    }

    // create a new max_pool_with_indices_bprop
    auto max_pool_with_indices_bprop =
        std::make_shared<op::MaxPoolWithIndicesBackprop>(pattern_map[data],
                                                         pattern_map[delta],
                                                         max_pool_with_indices_indices,
                                                         m_max_pool->get_window_shape(),
                                                         m_max_pool->get_window_movement_strides(),
                                                         m_max_pool->get_padding_below(),
                                                         m_max_pool->get_padding_above());

    ngraph::replace_node(m_max_pool_bprop, max_pool_with_indices_bprop);
    if (m_return_indices)
    {
        m_indices_list.push_back(max_pool_with_indices_indices);
    }
    return true;
}
