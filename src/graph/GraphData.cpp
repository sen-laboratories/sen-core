/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "GraphData.h"

#include "../relations/IdListAttr.h"
#include "../relations/QueryUtil.h"

#include <Message.h>
#include <MimeType.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>
#include <String.h>
#include <fs_attr.h>

#include <string.h>

#include <vector>

#include <sen/Sen.h>

#include <spdlog/spdlog.h>

namespace sen {
namespace graph {

namespace {

/** what a file says about its relation to a target */
struct RelationInfo {
	std::string	type;	// the relation type
	std::string	label;	// the label of the relation itself, if it has one
};

/** the type of a file for its colour: the semantic type if it has one, else its MIME type */
std::string
TypeOf(BNode& node)
{
	BString type;
	if (node.ReadAttrString("META:TYPE", &type) == B_OK && !type.IsEmpty())
		return type.String();
	char mime[B_MIME_TYPE_LENGTH];
	if (BNodeInfo(&node).GetType(mime) == B_OK)
		return mime;
	return "";
}

static const char kMetaMimeType[] = "application/x-vnd.Be-meta-mime";

/**
 * What a file in the MIME database (the file of a type: the entities, relations... that the ontologies define) is called: the type that it stands for, in words
 * ("Music album", not "x-vnd.sen-labs.entity.music.album"), and what kind of type it is as the group.
 */
bool
NameOfTypeFile(const entry_ref& ref, BNode& node, std::string* label, std::string* group)
{
	// (the file of a type has the semantic type that it stands for as META:TYPE: its own type is the one of the MIME database)
	char fileType[B_MIME_TYPE_LENGTH];
	if (BNodeInfo(&node).GetType(fileType) != B_OK || strcmp(fileType, kMetaMimeType) != 0)
		return false;
	BPath path(&ref), parent;
	if (path.InitCheck() != B_OK || path.GetParent(&parent) != B_OK || parent.Leaf() == NULL)
		return false;
	const std::string supertype = parent.Leaf();
	char shortName[B_MIME_TYPE_LENGTH];
	if (BMimeType((supertype + "/" + ref.name).c_str()).GetShortDescription(shortName) == B_OK && shortName[0] != '\0')
		*label = shortName;
	*group = "type: " + supertype;
	return true;
}

/** The relations in SEN:REL:* of a file: for each target (a field named by its SEN:ID) the type and the label of the first relation. */
void
ReadRelations(BNode& node, std::map<std::string, RelationInfo>* relations)
{
	const size_t prefixLength = strlen(sen::attr::kRelationPrefix);
	char name[B_ATTR_NAME_LENGTH];
	node.RewindAttrs();
	while (node.GetNextAttrName(name) == B_OK) {
		if (strncmp(name, sen::attr::kRelationPrefix, prefixLength) != 0)
			continue;
		attr_info info;
		if (node.GetAttrInfo(name, &info) != B_OK || info.type != B_MESSAGE_TYPE || info.size <= 0)
			continue;	// (the attributes of a file of a relation are no relations)
		std::vector<char> buffer(info.size);
		BMessage targets;
		if (node.ReadAttr(name, B_MESSAGE_TYPE, 0, buffer.data(), info.size) != info.size || targets.Unflatten(buffer.data()) != B_OK)
			continue;

		const std::string type = std::string("relation/") + (name + prefixLength);
		char* target;
		type_code code;
		int32 count;
		for (int32 index = 0; targets.GetInfo(B_MESSAGE_TYPE, index, &target, &code, &count) == B_OK; index++) {
			BMessage properties;
			if (targets.FindMessage(target, 0, &properties) != B_OK)
				continue;
			RelationInfo relation;
			relation.type = type;
			relation.label = properties.GetString(sen::attr::kRelationLabel, "");
			relations->emplace(target, relation);	// (the first one of a target stays)
		}
	}
}

/** the name of a relation type for people, when a relation has no label of its own */
std::string
NameOfType(const std::string& type, std::map<std::string, std::string>* names)
{
	auto found = names->find(type);
	if (found != names->end())
		return found->second;
	char shortName[B_MIME_TYPE_LENGTH];
	std::string name;
	if (!type.empty() && BMimeType(type.c_str()).GetShortDescription(shortName) == B_OK)
		name = shortName;
	(*names)[type] = name;
	return name;
}

}	// namespace


status_t
Collect(Collected* collected, size_t maxObjects)
{
	std::vector<entry_ref> refs;
	status_t result = sen::QueryAllVolumes((BString(sen::attr::kId) << "==*").String(), &refs);
	if (result != B_OK && refs.empty())
		return result;

	collected->objects = static_cast<int32>(refs.size());

	// the nodes
	for (const entry_ref& ref : refs) {
		if (collected->refs.size() >= maxObjects) {
			collected->truncated = true;
			break;
		}
		BNode node(&ref);
		BString id;
		if (node.InitCheck() != B_OK || node.ReadAttrString(sen::attr::kId, &id) != B_OK || id.IsEmpty())
			continue;
		if (collected->refs.count(id.String()) != 0)
			continue;	// (two files with one id: the first is the object)
		std::string type = TypeOf(node), label = ref.name, group = type;
		NameOfTypeFile(ref, node, &label, &group);
		collected->graph.AddNode(id.String(), label, group);
		collected->refs[id.String()] = ref;
	}

	// the edges
	std::map<std::string, std::string> typeNames;
	for (const auto& object : collected->refs) {
		BNode node(&object.second);
		if (node.InitCheck() != B_OK)
			continue;

		std::map<std::string, RelationInfo> relations;
		ReadRelations(node, &relations);

		const char* const lists[] = {sen::attr::kTo, sen::attr::kMeta};
		for (const char* list : lists) {
			std::vector<std::string> targets;
			if (sen::ReadIdList(node, list, &targets) != B_OK)
				continue;
			const bool meta = strcmp(list, sen::attr::kMeta) == 0;
			for (const std::string& target : targets) {
				std::string label, type;
				auto relation = relations.find(target);
				if (relation != relations.end()) {
					type = relation->second.type;
					label = relation->second.label;
				}
				if (label.empty())
					label = NameOfType(type, &typeNames);
				collected->graph.AddEdge(object.first, target, label, type, meta);
			}
		}
	}

	spdlog::debug("graph: {} objects, {} nodes, {} relations", collected->objects, collected->graph.Nodes().size(), collected->graph.Edges().size());
	return B_OK;
}

}	// namespace graph
}	// namespace sen
