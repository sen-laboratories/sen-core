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

#include <map>
#include <utility>
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

/** the labels that the definition of a relation type gives (`SEN:REL:CONFIG` in the MIME database): what it says forward ("cites") and the other way ("cited by"); empty if it does not say */
struct TypeLabels {
	std::string	forward, inverse;
};

const TypeLabels&
LabelsOfType(const std::string& type, std::map<std::string, TypeLabels>* cache)
{
	auto found = cache->find(type);
	if (found != cache->end())
		return found->second;

	TypeLabels labels;
	BPath path;
	if (!type.empty() && find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK && path.Append("mime_db") == B_OK && path.Append(type.c_str()) == B_OK) {
		BNode node(path.Path());
		attr_info info;
		if (node.InitCheck() == B_OK && node.GetAttrInfo(sen::attr::kRelationConfig, &info) == B_OK && info.size > 0 && info.size < 64 * 1024) {
			std::vector<char> buffer(info.size);
			BMessage config, relation, inverse;
			if (node.ReadAttr(sen::attr::kRelationConfig, B_MESSAGE_TYPE, 0, buffer.data(), info.size) == info.size && config.Unflatten(buffer.data()) == B_OK) {
				if (config.FindMessage(sen::conf::kRelation, &relation) == B_OK)
					labels.forward = relation.GetString(sen::attr::kRelationLabel, "");
				if (config.FindMessage(sen::conf::kInverse, &inverse) == B_OK)
					labels.inverse = inverse.GetString(sen::attr::kRelationLabel, "");
			}
		}
	}
	return (*cache)[type] = labels;
}

/** a relation as a file has it, before the labels are settled */
struct Pending {
	std::string	from, to, label, type;
	bool		meta;
};

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

	// the edges: first as the files have them
	std::map<std::string, std::string> typeNames;
	std::map<std::string, TypeLabels> typeLabels;
	std::vector<Pending> pending;
	std::map<std::pair<std::string, std::string>, size_t> index;
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
				Pending edge;
				edge.from = object.first;
				edge.to = target;
				edge.meta = meta;
				auto relation = relations.find(target);
				if (relation != relations.end()) {
					edge.type = relation->second.type;
					edge.label = relation->second.label;
				}
				index[{edge.from, edge.to}] = pending.size();
				pending.push_back(edge);
			}
		}
	}

	// a relation that has no label of its own says what its type says: "cites" forward and "cited by" the other way, not the name of the type ("Citation"). Of the two directions of a
	// relation that have none, the one of the file with the smaller id is the forward one (the files do not tell which of them made the relation).
	for (Pending& edge : pending) {
		if (!edge.label.empty())
			continue;
		const TypeLabels& labels = LabelsOfType(edge.type, &typeLabels);
		if (labels.forward.empty()) {
			edge.label = NameOfType(edge.type, &typeNames);
			continue;
		}
		auto opposite = index.find({edge.to, edge.from});
		if (opposite == index.end()) {
			edge.label = labels.forward;
		} else {
			const std::string& other = pending[opposite->second].label;
			if (!other.empty())
				edge.label = other == labels.forward && !labels.inverse.empty() ? labels.inverse : labels.forward;
			else
				edge.label = edge.from < edge.to || labels.inverse.empty() ? labels.forward : labels.inverse;
		}
	}

	for (const Pending& edge : pending)
		collected->graph.AddEdge(edge.from, edge.to, edge.label, edge.type, edge.meta);

	spdlog::debug("graph: {} objects, {} nodes, {} relations", collected->objects, collected->graph.Nodes().size(), collected->graph.Edges().size());
	return B_OK;
}

}	// namespace graph
}	// namespace sen
