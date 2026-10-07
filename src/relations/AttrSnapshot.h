/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Entry.h>
#include <Node.h>
#include <fs_attr.h>

#include <spdlog/spdlog.h>

#include <string>
#include <vector>

/**
 * @file AttrSnapshot.h
 * @brief Makes changes of several attributes of several files all-or-nothing.
 *
 * Adding or removing a relation writes the relation of the source, its list of targets, and the same for the opposite
 * direction at the target: four attributes (and the chunks of the lists) in two files. Before an operation changes an
 * attribute it is captured here; if a later step fails, Restore() puts everything back as it was.
 */
namespace sen {

class AttrSnapshot {
public:
    /**
     * @brief Remember the current state of attributes of a file (also that they are not there). An attribute that was
     * captured already is not captured again: the first state is the one to return to.
     */
    void Capture(const entry_ref& ref, const std::vector<std::string>& attrNames)
    {
        BNode node(&ref);
        if (node.InitCheck() != B_OK)
            return;

        for (const std::string& name : attrNames) {
            if (Has(ref, name))
                continue;

            Saved saved;
            saved.ref = ref;
            saved.name = name;

            attr_info info;
            if (node.GetAttrInfo(name.c_str(), &info) == B_OK) {
                saved.present = true;
                saved.type = info.type;
                saved.data.resize(info.size);
                if (info.size > 0 && node.ReadAttr(name.c_str(), info.type, 0, saved.data.data(), info.size) != info.size)
                    continue;   // cannot capture: better not to touch it than to restore garbage
            }
            fSaved.push_back(saved);
        }
    }

    /** @brief Put all captured attributes back. Errors are logged and do not stop the others. */
    void Restore()
    {
        for (const Saved& saved : fSaved) {
            BNode node(&saved.ref);
            if (node.InitCheck() != B_OK)
                continue;

            if (saved.present) {
                ssize_t written = node.WriteAttr(saved.name.c_str(), saved.type, 0, saved.data.data(), saved.data.size());
                if (written != (ssize_t) saved.data.size())
                    spdlog::error("failed to restore attribute {} of {}", saved.name, saved.ref.name);
            } else {
                node.RemoveAttr(saved.name.c_str());
            }
        }
        fSaved.clear();
    }

private:
    struct Saved {
        entry_ref         ref;
        std::string       name;
        bool              present = false;
        type_code         type = 0;
        std::vector<char> data;
    };

    bool Has(const entry_ref& ref, const std::string& name) const
    {
        for (const Saved& saved : fSaved) {
            if (saved.ref == ref && saved.name == name)
                return true;
        }
        return false;
    }

    std::vector<Saved> fSaved;
};

}   // namespace sen
