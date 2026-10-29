/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

/**
 * Reads the graph of the objects like the graph window does (graph::Collect) and checks what is there: nodes and edges, that the files of the types of
 * the MIME database are named in words ("Music album", not "x-vnd.sen-labs.entity.music.album") and that the relations have a label. Does not need the
 * server. Haiku only. With an argument the whole graph is printed.
 */
#include <stdio.h>
#include <string.h>

#include <FindDirectory.h>
#include <Path.h>

#include <sen/SenLookup.h>

#include "../../src/graph/GraphData.h"

static int failed = 0;

#define CHECK(condition, message) \
	do { \
		if (condition) \
			printf("ok   %s\n", message); \
		else { \
			printf("FAIL %s\n", message); \
			failed++; \
		} \
	} while (0)

/** the file of a type in the MIME database and an ordinary file are told apart by the SEN API (SenLookup.h) */
static void
CheckTypeFunctions()
{
	BPath settings;
	find_directory(B_USER_SETTINGS_DIRECTORY, &settings);

	BPath typeFile(settings.Path(), "mime_db/entity/x-vnd.sen-labs.entity.book");
	entry_ref ref;
	if (get_ref_for_path(typeFile.Path(), &ref) == B_OK) {
		CHECK(sen::lookup::TypeOfDatabaseEntry(ref) == "entity/x-vnd.sen-labs.entity.book", "the file of a type stands for the type");
		BNode node(&ref);
		CHECK(sen::lookup::TypeOf(node) == "entity/x-vnd.sen-labs.entity.book", "the type of the file is its META:TYPE");
	} else
		printf("     (no entity/x-vnd.sen-labs.entity.book in the MIME database: not checked)\n");

	BPath ontology(settings.Path(), "sen/ontologies/core");
	if (get_ref_for_path(ontology.Path(), &ref) == B_OK) {
		CHECK(sen::lookup::TypeOfDatabaseEntry(ref).empty(), "an ontology stands for no type (it has the type of ontologies)");
		BNode node(&ref);
		CHECK(sen::lookup::TypeOf(node) == "application/x-vnd.sen-labs.ontology", "the type of an ontology");
	}

	CHECK(sen::lookup::NameOfType("entity/x-vnd.sen-labs.entity.book") == "Book", "the name of a type is its short description");
	CHECK(sen::lookup::NameOfType("application/x-vnd.no.such.type") == "application/x-vnd.no.such.type", "a type without a description is its own name");
}

int
main(int argc, char**)
{
	CheckTypeFunctions();
	sen::graph::Collected collected;
	status_t result = sen::graph::Collect(&collected, 5000);
	CHECK(result == B_OK, "the objects can be read");
	const sen::graph::Graph& graph = collected.graph;
	printf("     %d objects, %d nodes, %d relations\n", (int)collected.objects, (int)graph.Nodes().size(), (int)graph.Edges().size());
	CHECK(!graph.Nodes().empty(), "there are nodes");
	CHECK(!graph.Edges().empty(), "there are relations");

	// the ontologies (files of the ontology type) are marked, and only those; the filter leaves them and their relations out
	int ontologies = 0, wrongMark = 0, ontologyEdges = 0;
	for (const sen::graph::Node& node : graph.Nodes()) {
		const bool hasType = node.group == "application/x-vnd.sen-labs.ontology";
		if (node.ontology)
			ontologies++;
		if (node.ontology != hasType)
			wrongMark++;
	}
	for (const sen::graph::Edge& edge : graph.Edges()) {
		if (graph.Nodes()[edge.from].ontology || graph.Nodes()[edge.to].ontology)
			ontologyEdges++;
	}
	printf("     %d ontologies, %d relations at them\n", ontologies, ontologyEdges);
	CHECK(wrongMark == 0, "a node is an ontology exactly when its file has the type of the ontologies");
	const sen::graph::Graph without = graph.Filtered(true, true, false);
	CHECK(without.Nodes().size() == graph.Nodes().size() - ontologies, "without the ontologies only they are gone");
	CHECK(without.Edges().size() == graph.Edges().size() - ontologyEdges, "without the ontologies their relations are gone");
	bool left = false;
	for (const sen::graph::Node& node : without.Nodes())
		left = left || node.ontology;
	CHECK(!left, "no ontology is left");
	CHECK(graph.Filtered(true, true).Nodes().size() == graph.Nodes().size(), "with the ontologies (the default) all nodes are there");

	int typeNodes = 0, technical = 0;
	for (const sen::graph::Node& node : graph.Nodes()) {
		if (node.group.compare(0, 6, "type: ") != 0)
			continue;
		typeNodes++;
		if (node.label.compare(0, 6, "x-vnd.") == 0)
			technical++;
		if (argc > 1)
			printf("     type  %-28s %s\n", node.label.c_str(), node.group.c_str());
	}
	printf("     %d nodes stand for types, %d of them are named by their file names\n", typeNodes, technical);
	CHECK(typeNodes == 0 || technical == 0, "the types are named in words");

	int unlabelled = 0;
	for (const sen::graph::Edge& edge : graph.Edges()) {
		if (edge.label.empty())
			unlabelled++;
		if (argc > 1)
			printf("     edge  %s %s %s  [%s]  %s\n", graph.Nodes()[edge.from].label.c_str(), edge.reciprocal ? "<->" : "->", graph.Nodes()[edge.to].label.c_str(),
				edge.label.c_str(), edge.inverseLabel.c_str());
	}
	printf("     %d relations without a label\n", unlabelled);
	CHECK(unlabelled == 0, "all relations have a label");

	printf("%s\n", failed == 0 ? "all ok" : "FAILED");
	return failed == 0 ? 0 : 1;
}
