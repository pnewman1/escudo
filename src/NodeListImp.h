/*
 * Copyright 2010-2012 Esrille Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ORG_W3C_DOM_BOOTSTRAP_NODELISTIMP_H_INCLUDED
#define ORG_W3C_DOM_BOOTSTRAP_NODELISTIMP_H_INCLUDED

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <org/w3c/dom/NodeList.h>

#include <deque>

#include <org/w3c/dom/Node.h>

namespace org { namespace w3c { namespace dom { namespace bootstrap {

class NodeListImp : public ObjectMixin<NodeListImp>
{
    std::deque<Node> list;

public:
    void addItem(Node node) {
        list.push_back(node);
    }

    // NodeList
    virtual Node item(unsigned int index) {
        if (getLength() <= index)
            return 0;
        else
            return list[index];
    }
    virtual unsigned int getLength() {
        return list.size();
    }
    // Object
    virtual Any message_(uint32_t selector, const char* id, int argc, Any* argv)
    {
        return NodeList::dispatch(this, selector, id, argc, argv);
    }
    static const char* const getMetaData()
    {
        return NodeList::getMetaData();
    }
};


}}}}  // org::w3c::dom::bootstrap

#endif  // ORG_W3C_DOM_BOOTSTRAP_NODELISTIMP_H_INCLUDED
