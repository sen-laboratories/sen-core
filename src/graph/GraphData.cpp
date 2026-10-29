/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "GraphData.h"

#include "../relations/IdListAttr.h"
#include "../relations/QueryUtil.h"

#include <FindDirectory.h>
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
#include <sen/SenLookup.h>

#include <spdlog/spdlog.h>

namespace sen {
namespace graph {

namespace {

/** what a file says about its relation to a target */
struct RelationInfo {
	std::string	type;	// the relation type
	std::string	label;	// the label of the relation itself, if it has one
};

/**
 * The label and the group of the node of a file. A file of the MIME database stands for a type (the entities, relations... that the ontologies define): it
 * is named as the type is ("Music album", not "x-vnd.sen-labs.entity.music.album") and its group is what kind of type it is. Any other file is named
 * like the file and grouped by its type.
 */
void
NameOfNode(const entry_ref& ref, BNode& node, std::string* label, std::string* group)
{
	const std::string stands = sen::lookup::TypeOfDatabaseEntry(ref);
	if (!stands.empty()) {
		*label = sen::lookup::NameOfType(stands.c_str());
		if (*label == stands)
			*label = ref.name;	// (a type without a description)
		*group = "type: " + stands.substr(0, stands.find('/'));
		return;
	}
	*label = ref.name;
	*group = sen::lookup::TypeOf(node);
}

/** Is it the file of an ontology: of the type of the ontologies (as Akita shows it as a folder)? */
bool
IsOntology(BNode& node)
{
	char type[B_MIME_TYPE_LENGTH];
	return BNodeInfo(&node).GetType(type) == B_OK && strcasecmp(type, sen::mime::kOntology) == 0;
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
		std::string label, group;
		NameOfNode(ref, node, &label, &group);
		collected->graph.AddNode(id.String(), label, group, IsOntology(node));
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
