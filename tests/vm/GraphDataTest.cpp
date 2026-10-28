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

int
main(int argc, char**)
{
	sen::graph::Collected collected;
	status_t result = sen::graph::Collect(&collected, 5000);
	CHECK(result == B_OK, "the objects can be read");
	const sen::graph::Graph& graph = collected.graph;
	printf("     %d objects, %d nodes, %d relations\n", (int)collected.objects, (int)graph.Nodes().size(), (int)graph.Edges().size());
	CHECK(!graph.Nodes().empty(), "there are nodes");
	CHECK(!graph.Edges().empty(), "there are relations");

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
