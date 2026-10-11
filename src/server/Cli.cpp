/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "Cli.h"

#include "../stats/SenStats.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include <String.h>

using namespace sen::stats;

namespace {

/** The number of characters (not bytes) of a text: the width of it in a terminal, for the columns. */
int32
Width(const BString& text)
{
	return text.CountChars();
}

/** The types of a group as a tree, in three columns: the name of the type (as people read it), the type, and the number of files. */
void
PrintTypeTree(const TypeGroup& group)
{
	printf("%s  %s \xc2\xb7 %s\n", group.supertype.String(), Plural((int32)group.types.size(), "type", "types").String(),
		Plural(group.files, "file", "files").String());

	// the rows with the lines of the tree in front, then the columns can be aligned
	struct Row {
		BString		tree;	// the lines of the ancestors and this branch
		BString		name;
		BString		type;
		int32		files;
	};
	std::vector<Row> rows;
	struct Collector {
		const TypeGroup& group;
		std::vector<Row>* rows;
		void Collect(size_t index, const BString& prefix, bool last) const
		{
			const TypeNode& node = group.types[index];
			Row row;
			row.tree = prefix;
			row.tree << (last ? "\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80 " : "\xe2\x94\x9c\xe2\x94\x80\xe2\x94\x80 ");
			row.name = node.name;
			row.type = node.subtype;
			row.files = node.files;
			rows->push_back(row);

			BString childPrefix(prefix);
			childPrefix << (last ? "    " : "\xe2\x94\x82   ");
			for (size_t child = 0; child < node.children.size(); child++)
				Collect(node.children[child], childPrefix, child + 1 == node.children.size());
		}
	} collector{group, &rows};
	for (size_t root = 0; root < group.roots.size(); root++)
		collector.Collect(group.roots[root], BString(), root + 1 == group.roots.size());

	int32 nameWidth = 0, typeWidth = 0, countWidth = 1;
	for (const Row& row : rows) {
		nameWidth = std::max(nameWidth, Width(row.tree) + Width(row.name));
		typeWidth = std::max(typeWidth, Width(row.type));
		BString count;
		count << row.files;
		countWidth = std::max(countWidth, Width(count));
	}
	for (const Row& row : rows) {
		printf("%s%s", row.tree.String(), row.name.String());
		for (int32 pad = Width(row.tree) + Width(row.name); pad < nameWidth + 2; pad++)
			putchar(' ');
		printf("%s", row.type.String());
		for (int32 pad = Width(row.type); pad < typeWidth + 2; pad++)
			putchar(' ');
		printf("%*d\n", (int)countWidth, (int)row.files);
	}
}


// ---- the commands

int
Version()
{
	printf("%s\n", VersionLine().String());
	return 0;
}

int
Info()
{
	Overview overview = GatherOverview();
	printf("%s\n", overview.version.String());
	printf("Server     %s\n", overview.serverRunning ? "running" : "not running");

	BString perVolume, notCounted;
	for (const VolumeObjects& volume : overview.volumes) {
		BString& list = volume.indexed ? perVolume : notCounted;
		if (volume.indexed && volume.objects == 0)
			continue;
		if (!list.IsEmpty())
			list << ", ";
		list << volume.name;
		if (volume.indexed)
			list << " " << volume.objects;
	}
	printf("Objects    %d", (int)overview.objects);
	if (!perVolume.IsEmpty())
		printf("  (%s)", perVolume.String());
	printf("\n");
	if (!notCounted.IsEmpty())
		printf("           not counted, no index for SEN:ID (see create-sen-indices.sh): %s\n", notCounted.String());

	printf("Types      %s\n", TypesSummary(overview).String());
	printf("Relations  %d\n", (int)overview.relations);
	return 0;
}

int
Types()
{
	std::vector<BString> notes;
	std::vector<TypeGroup> groups = GatherTypes(&notes);

	for (size_t group = 0; group < groups.size(); group++) {
		if (group > 0)
			printf("\n");
		if (groups[group].types.empty())
			printf("%s  no types installed\n", groups[group].supertype.String());
		else
			PrintTypeTree(groups[group]);
	}

	for (const BString& name : notes)
		printf("\nVolume %s cannot query the type of files: only its files with a SEN:ID are counted.\n", name.String());
	return 0;
}

int
Check()
{
	CheckResult result = RunCheck();
	printf("Checked %s on %s.\n", Plural(result.objects, "SEN object", "SEN objects").String(),
		Plural(result.volumes, "volume", "volumes").String());

	for (const Finding& finding : result.findings) {
		printf("PROBLEM: %s\n", finding.summary.String());
		for (const BString& detail : finding.details)
			printf("    %s\n", detail.String());
	}

	if (result.findings.empty())
		printf("No problems found.\n");
	else
		printf("%s found.\n", Plural((int32)result.findings.size(), "problem", "problems").String());
	return result.findings.empty() ? 0 : 1;
}

void
Usage(FILE* out, const char* program)
{
	fprintf(out,
		"usage: %s [option]\n"
		"Without an option the SEN server is started. Options:\n"
		"  --version  the name and version of the server\n"
		"  --info     an overview: SEN objects, types and relations installed\n"
		"  --types    the installed types and relations as a tree, with the number of files of each\n"
		"  --check    look for inconsistencies: duplicate SEN:IDs, files of types that are not installed, missing indices\n"
		"  --help     this text\n",
		program);
}

}	// namespace


int
RunCommandLine(int argc, char* argv[])
{
	if (argc < 2)
		return -1;

	const char* option = argv[1];
	if (strcmp(option, "--version") == 0)
		return Version();
	if (strcmp(option, "--info") == 0)
		return Info();
	if (strcmp(option, "--types") == 0)
		return Types();
	if (strcmp(option, "--check") == 0)
		return Check();
	if (strcmp(option, "--help") == 0 || strcmp(option, "-h") == 0) {
		Usage(stdout, argv[0]);
		return 0;
	}

	fprintf(stderr, "unknown option '%s'\n", option);
	Usage(stderr, argv[0]);
	return 2;
}
