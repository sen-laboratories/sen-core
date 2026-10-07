/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

/**
 * Performance of the lookup of a SEN:ID in the chunked lists SEN:TO, SEN:TO:1 ... (see sen::idlist).
 *
 * The lookup "which files have this id in their targets" is a query with a wildcard on both sides, over all attributes of the
 * list joined by OR. This program fills a folder with files whose lists have a realistic or an extreme length, and times
 * the query with 1, 2, 6 and 8 attributes, with the indices of the higher chunks and without them.
 *
 *   QueryPerf <folder on a BFS volume> <files> sparse|dense
 *
 * Needs the indices of the core ontology (SEN:TO and SEN:TO:1 ... SEN:TO:7), run on a Haiku.
 */

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <Node.h>
#include <Path.h>
#include <Query.h>
#include <Volume.h>
#include <fs_index.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chrono>
#include <string>
#include <vector>

#include <sen/Sen.h>

#include "../../../src/relations/IdListAttr.h"

struct Profile {
	const char*	name;
	// how many files have lists of 3, 20, 40, 90 and 128 ids (1, 2, 3, 6 and 8 attributes), per 1000 files
	int			share[5];
};

static const Profile kProfiles[] = {
	{"sparse", {960, 30, 8, 2, 0}},		// most files link a few others, few link many
	{"dense", {700, 200, 70, 20, 10}},	// extreme: 30% of the files have more than 16 targets
};

static const int kListSizes[5] = {3, 20, 40, 90, 128};

static double
Now()
{
	using namespace std::chrono;
	return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

/** the query over the first `terms` attributes of the list, for the id */
static std::string
Predicate(int terms, const std::string& id)
{
	std::string predicate;
	for (int i = 0; i < terms; i++) {
		if (i > 0)
			predicate += " || ";
		predicate += sen::idlist::ChunkName(sen::attr::kTo, i) + " == '*" + id + "*'";
	}
	return predicate;
}

/** runs the query, returns the average time in ms over `rounds` runs after one warm-up; the number found goes to *found */
static double
TimeQuery(const BVolume& volume, const std::string& predicate, int rounds, int* found, status_t* error)
{
	*error = B_OK;
	double total = 0;
	for (int round = 0; round <= rounds; round++) {
		double start = Now();
		BQuery query;
		query.SetVolume(&volume);
		query.SetPredicate(predicate.c_str());
		status_t status = query.Fetch();
		int count = 0;
		entry_ref ref;
		if (status == B_OK) {
			while (query.GetNextRef(&ref) == B_OK)
				count++;
		}
		double elapsed = Now() - start;
		*error = status;
		*found = count;
		if (status != B_OK)
			return 0;
		if (round > 0)
			total += elapsed;
	}
	return total / rounds;
}

static void
Report(const BVolume& volume, const char* label, int terms, const std::string& id)
{
	int found = 0;
	status_t error;
	double ms = TimeQuery(volume, Predicate(terms, id), 20, &found, &error);
	if (error != B_OK)
		printf("  %-34s %2d terms: query failed: %s\n", label, terms, strerror(error));
	else
		printf("  %-34s %2d terms: %8.2f ms   (found %d)\n", label, terms, ms, found);
}

int
main(int argc, char** argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: %s <folder> <files> sparse|dense\n", argv[0]);
		return 2;
	}
	const char* folder = argv[1];
	int files = atoi(argv[2]);
	const Profile* profile = NULL;
	for (const Profile& candidate : kProfiles) {
		if (strcmp(candidate.name, argv[3]) == 0)
			profile = &candidate;
	}
	if (profile == NULL || files < 100) {
		fprintf(stderr, "profile must be sparse or dense, at least 100 files\n");
		return 2;
	}

	system((std::string("rm -rf '") + folder + "' && mkdir -p '" + folder + "'").c_str());
	BDirectory directory(folder);
	BPath path(folder);
	dev_t device = 0;
	{
		BEntry entry(folder);
		node_ref nodeRef;
		entry.GetNodeRef(&nodeRef);
		device = nodeRef.device;
	}
	BVolume volume(device);

	// the id that we look for: it is in a file of every list size, at a position in the last attribute of that list
	std::string wanted = sen::id::New();
	int placed = 0;

	printf("== profile %s, %d files\n", profile->name, files);
	double start = Now();
	int fileIndex = 0;
	for (int group = 0; group < 5; group++) {
		int count = files * profile->share[group] / 1000;
		for (int i = 0; i < count; i++, fileIndex++) {
			std::vector<std::string> ids;
			int size = kListSizes[group];
			for (int k = 0; k < size; k++)
				ids.push_back(sen::id::New());
			if (i == 0 && size > 3) {
				// last position of the list = the last chunk it uses
				ids[size - 1] = wanted;
				placed++;
			} else if (i == 0 && group == 0) {
				ids[1] = wanted;
				placed++;
			}
			BString name("f");
			name << fileIndex;
			BFile file(&directory, name.String(), B_CREATE_FILE | B_READ_WRITE);
			sen::WriteIdList(file, sen::attr::kTo, ids);
		}
	}
	printf("  created %d files in %.1f s, the id is in %d of them (one per list size)\n", fileIndex, (Now() - start) / 1000, placed);

	printf("-- all chunks indexed\n");
	for (int terms : {1, 2, 6, 8})
		Report(volume, "indexed", terms, wanted);

	// an id that is not there: the cost of a query that finds nothing (the common case when checking for back links)
	std::string absent = sen::id::New();
	Report(volume, "indexed, id not present", 8, absent);

	printf("-- the indices of the chunks 6 and 7 removed (a list can still be 8 attributes long)\n");
	fs_remove_index(device, "SEN:TO:6");
	fs_remove_index(device, "SEN:TO:7");
	for (int terms : {6, 8})
		Report(volume, "chunks 6,7 not indexed", terms, wanted);

	// put the indices back and make the files known to them again
	fs_create_index(device, "SEN:TO:6", B_STRING_TYPE, 0);
	fs_create_index(device, "SEN:TO:7", B_STRING_TYPE, 0);
	{
		BDirectory dir(folder);
		BEntry entry;
		while (dir.GetNextEntry(&entry) == B_OK) {
			BNode node(&entry);
			for (int chunk : {6, 7}) {
				BString value;
				std::string name = sen::idlist::ChunkName(sen::attr::kTo, chunk);
				if (node.ReadAttrString(name.c_str(), &value) == B_OK)
					node.WriteAttrString(name.c_str(), &value);
			}
		}
	}

	printf("-- the same after restoring the indices\n");
	Report(volume, "restored", 8, wanted);

	system((std::string("rm -rf '") + folder + "'").c_str());
	return 0;
}
