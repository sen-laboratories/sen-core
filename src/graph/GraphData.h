/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Entry.h>
#include <SupportDefs.h>

#include <map>
#include <string>

#include <sen/SenGraph.h>

namespace sen {
namespace graph {

/** The objects of SEN and their relations, as they are on the disk. */
struct Collected {
	Graph							graph;			///< a node for each file that has a SEN:ID, an edge for each entry of its SEN:TO (and SEN:META) list
	std::map<std::string, entry_ref>	refs;		///< the file of each node, by SEN:ID
	int32							objects = 0;	///< how many objects there are (more than the graph has, if it is cut)
	bool							truncated = false;
};

/**
 * Read the graph from the volumes (all that can be queried): the files with a SEN:ID are the nodes (named like the file, coloured by the type), the ids in
 * SEN:TO and SEN:META of a file are the edges to the nodes of that id. What the relation is called is in the relations of the file (the attributes
 * SEN:REL:<type>, a field for each target, with the properties of the relation: its label); without the label of the relation itself the name of its
 * type is used. Can take a while: it is made for a thread of its own.
 * @param maxObjects the most nodes that are read (the rest is not in the graph, see Collected::truncated)
 */
status_t Collect(Collected* collected, size_t maxObjects);

}	// namespace graph
}	// namespace sen
