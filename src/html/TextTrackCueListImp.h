// Generated by esidl 0.3.0.
// This file is expected to be modified for the Web IDL interface
// implementation.  Permission to use, copy, modify and distribute
// this file in any software license is hereby granted.

#ifndef ORG_W3C_DOM_BOOTSTRAP_TEXTTRACKCUELISTIMP_H_INCLUDED
#define ORG_W3C_DOM_BOOTSTRAP_TEXTTRACKCUELISTIMP_H_INCLUDED

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <org/w3c/dom/html/TextTrackCueList.h>

#include <org/w3c/dom/html/TextTrackCue.h>

namespace org
{
namespace w3c
{
namespace dom
{
namespace bootstrap
{
class TextTrackCueListImp : public ObjectMixin<TextTrackCueListImp>
{
public:
    // TextTrackCueList
    unsigned int getLength();
    html::TextTrackCue getElement(unsigned int index);
    html::TextTrackCue getCueById(const std::u16string& id);
    // Object
    virtual Any message_(uint32_t selector, const char* id, int argc, Any* argv)
    {
        return html::TextTrackCueList::dispatch(this, selector, id, argc, argv);
    }
    static const char* const getMetaData()
    {
        return html::TextTrackCueList::getMetaData();
    }
};

}
}
}
}

#endif  // ORG_W3C_DOM_BOOTSTRAP_TEXTTRACKCUELISTIMP_H_INCLUDED