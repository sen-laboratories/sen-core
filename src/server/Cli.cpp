/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "Cli.h"

#include <sen/Sen.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <map>
#include <set>
#include <utility>
#include <string>
#include <vector>

#include <AppFileInfo.h>
#include <Entry.h>
#include <File.h>
#include <Message.h>
#include <MimeType.h>
#include <Node.h>
#include <NodeInfo.h>
#include <OS.h>
#include <Path.h>
#include <Query.h>
#include <Roster.h>
#include <StringList.h>
#include <Volume.h>
#include <VolumeRoster.h>
#include <fs_index.h>

namespace {

// ---- volumes and queries

struct VolumeInfo {
	dev_t		device;
	BString		name;
	bool		hasIndexOf(const char* attribute) const
	{
		index_info info;
		return fs_stat_index(device, attribute, &info) == 0;
	}
};

/** The volumes that SEN can work with: those that know queries and attributes. */
std::vector<VolumeInfo>
QueryableVolumes()
{
	std::vector<VolumeInfo> volumes;
	BVolumeRoster roster;
	BVolume volume;
	while (roster.GetNextVolume(&volume) == B_OK) {
		// (a read-only volume, e.g. the packagefs of /system, cannot be changed, so SEN does not work with it, see SenServer::WatchVolume)
		if (!volume.KnowsQuery() || !volume.KnowsAttr() || volume.IsReadOnly())
			continue;
		VolumeInfo info;
		info.device = volume.Device();
		char name[B_FILE_NAME_LENGTH];
		info.name = volume.GetName(name) == B_OK && name[0] != '\0' ? name : "(unnamed)";
		volumes.push_back(info);
	}
	return volumes;
}

/** Run a query on a volume and call @p each with every entry found; false if the query failed (e.g. no index). */
template <typename Each>
bool
QueryVolume(const VolumeInfo& volume, const char* predicate, Each each)
{
	BVolume bvolume(volume.device);
	BQuery query;
	if (query.SetVolume(&bvolume) != B_OK || query.SetPredicate(predicate) != B_OK || query.Fetch() != B_OK)
		return false;
	entry_ref ref;
	while (query.GetNextRef(&ref) == B_OK)
		each(ref);
	return true;
}

int32
CountEntries(const VolumeInfo& volume, const char* predicate)
{
	int32 count = 0;
	QueryVolume(volume, predicate, [&count](const entry_ref&) { count++; });
	return count;
}

BString
PathOf(const entry_ref& ref)
{
	BPath path(&ref);
	return BString(path.InitCheck() == B_OK ? path.Path() : ref.name);
}

BString
Plural(int32 count, const char* singular, const char* plural)
{
	BString text;
	text << count << " " << (count == 1 ? singular : plural);
	return text;
}


// ---- the application resource

/** The path of the program (the image of the team that is the application). */
bool
ExecutablePath(BPath* path)
{
	image_info info;
	int32 cookie = 0;
	while (get_next_image_info(0, &cookie, &info) == B_OK) {
		if (info.type == B_APP_IMAGE) {
			*path = BPath(info.name);
			return path->InitCheck() == B_OK;
		}
	}
	return false;
}

// (the values of B_APPV_DEVELOPMENT... of the resource compiler, they are not in a header)
const char*
VarietyName(uint32 variety)
{
	switch (variety) {
		case 0:	return "development";
		case 1:	return "alpha";
		case 2:	return "beta";
		case 3:	return "gamma";
		case 4:	return "golden master";
		default:	return "";	// final
	}
}

/** "SEN Server 0.2.0 alpha (2026-10-11, build 0)": name and version of the application resource, the day the program was made and the build. */
BString
VersionLine()
{
	BString line("SEN Server");
	BPath path;
	if (!ExecutablePath(&path))
		return line << " (unknown version)";

	BFile file(path.Path(), B_READ_ONLY);
	BAppFileInfo appInfo(&file);
	version_info version;
	if (file.InitCheck() != B_OK || appInfo.GetVersionInfo(&version, B_APP_VERSION_KIND) != B_OK)
		return line << " (unknown version)";

	line.SetTo(version.short_info[0] != '\0' ? version.short_info : "SEN Server");
	line << " " << (int)version.major << "." << (int)version.middle << "." << (int)version.minor;
	const char* variety = VarietyName(version.variety);
	if (variety[0] != '\0')
		line << " " << variety;

	time_t modified = 0;
	file.GetModificationTime(&modified);
	char date[16] = "";
	struct tm tm;
	if (modified != 0 && localtime_r(&modified, &tm) != NULL)
		strftime(date, sizeof(date), "%Y-%m-%d", &tm);
	line << " (";
	if (date[0] != '\0')
		line << date << ", ";
	line << "build " << (int)version.internal << ")";
	return line;
}


// ---- types

struct TypeInfo {
	std::string		type;
	BString			name;
	int32			files;
	std::vector<size_t>	children;
};

/** The installed types of a supertype, with their descriptive names (the counts are filled in later). */
std::vector<TypeInfo>
InstalledTypes(const char* supertype)
{
	std::vector<TypeInfo> types;
	BMessage installed;
	if (BMimeType::GetInstalledTypes(supertype, &installed) != B_OK)
		return types;

	const char* type;
	for (int32 index = 0; installed.FindString("types", index, &type) == B_OK; index++) {
		TypeInfo info;
		info.type = type;
		char shortName[B_MIME_TYPE_LENGTH];
		BMimeType mime(type);
		info.name = mime.GetShortDescription(shortName) == B_OK && shortName[0] != '\0' ? shortName : "";
		info.files = 0;
		types.push_back(info);
	}
	std::sort(types.begin(), types.end(), [](const TypeInfo& a, const TypeInfo& b) { return a.type < b.type; });
	return types;
}

/**
 * The number of files of every type and of files with relations of every kind. The files of a type are queried (the type, BEOS:TYPE, is
 * indexed by the file system itself), which finds all of them; where that is not possible only the files with a SEN:ID are counted (and
 * the volume is named in @p notes). A relation is counted for every file that has it, at its source
 * (SEN:REL:<name of the type>) and at its target, for the opposite direction.
 */
struct Counts {
	std::map<std::string, int32>	typeFiles;
	std::map<std::string, int32>	relationFiles;
	std::vector<BString>			notes;
};

void
CountFiles(const std::vector<VolumeInfo>& volumes, const std::vector<TypeInfo>& typesToCount, Counts* counts)
{
	for (const VolumeInfo& volume : volumes) {
		std::map<std::string, int32> scanned;

		if (volume.hasIndexOf(sen::attr::kId)) {
			QueryVolume(volume, (BString(sen::attr::kId) << "==*").String(), [&](const entry_ref& ref) {
				BNode node(&ref);
				if (node.InitCheck() != B_OK)
					return;
				char type[B_MIME_TYPE_LENGTH];
				if (BNodeInfo(&node).GetType(type) == B_OK && type[0] != '\0')
					scanned[type]++;

				char name[B_ATTR_NAME_LENGTH];
				node.RewindAttrs();
				while (node.GetNextAttrName(name) == B_OK) {
					if (strncmp(name, "SEN:REL:", 8) == 0)
						counts->relationFiles[name + 8]++;
				}
			});
		}

		// the type of files is indexed by the file system itself (BEOS:TYPE does not show up in the list of indices)
		bool queried = !typesToCount.empty();
		for (const TypeInfo& info : typesToCount) {
			BString predicate("BEOS:TYPE==\"");
			predicate << info.type.c_str() << "\"";
			int32 files = 0;
			if (!QueryVolume(volume, predicate.String(), [&files](const entry_ref&) { files++; })) {
				queried = false;
				break;
			}
			counts->typeFiles[info.type] += files;
		}
		if (!queried && !typesToCount.empty()) {
			// (a file system that cannot query the type: only the files with a SEN:ID are counted)
			for (const auto& entry : scanned)
				counts->typeFiles[entry.first] += entry.second;
			counts->notes.push_back(volume.name);
		}
	}
}

int32
CountInstalledTypes(const char* supertype)
{
	BMessage installed;
	if (BMimeType::GetInstalledTypes(supertype, &installed) != B_OK)
		return 0;
	type_code type;
	int32 count = 0;
	installed.GetInfo("types", &type, &count);
	return count;
}

/** The number of characters (not bytes) of a text: the width of it in a terminal, for the columns. */
int32
Width(const BString& text)
{
	return text.CountChars();
}

/**
 * The types of a supertype as a tree: a type whose name starts with the name of another type and a dot (the movie medium of a movie) is its
 * child. Three columns: the name of the type (as people read it), the type, and the number of files, right aligned.
 */
void
PrintTypeTree(const char* supertype, std::vector<TypeInfo>& types)
{
	// parents: the longest other type that the name continues with a dot
	std::vector<int> parent(types.size(), -1);
	for (size_t i = 0; i < types.size(); i++) {
		size_t best = 0;
		for (size_t j = 0; j < types.size(); j++) {
			const std::string& candidate = types[j].type;
			if (i != j && types[i].type.size() > candidate.size() + 1 && types[i].type.compare(0, candidate.size(), candidate) == 0
					&& types[i].type[candidate.size()] == '.' && candidate.size() > best) {
				best = candidate.size();
				parent[i] = (int)j;
			}
		}
	}
	std::vector<size_t> roots;
	for (size_t i = 0; i < types.size(); i++) {
		if (parent[i] < 0)
			roots.push_back(i);
		else
			types[parent[i]].children.push_back(i);
	}

	int32 total = 0;
	for (const TypeInfo& type : types)
		total += type.files;
	printf("%s  %s \xc2\xb7 %s\n", supertype, Plural((int32)types.size(), "type", "types").String(),
		Plural(total, "file", "files").String());

	// the rows with the lines of the tree in front, then the columns can be aligned
	struct Row {
		BString		tree;	// the lines of the ancestors and this branch
		BString		name;
		BString		type;
		int32		files;
	};
	std::vector<Row> rows;
	struct Collector {
		const std::vector<TypeInfo>& types;
		std::vector<Row>* rows;
		void Collect(size_t index, const BString& prefix, bool last) const
		{
			const TypeInfo& type = types[index];
			Row row;
			row.tree = prefix;
			row.tree << (last ? "\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80 " : "\xe2\x94\x9c\xe2\x94\x80\xe2\x94\x80 ");
			// the name that people read, else the subtype
			row.name = type.name;
			if (row.name.IsEmpty())
				row.name = type.type.substr(type.type.find('/') + 1).c_str();
			row.type = type.type.substr(type.type.find('/') + 1).c_str();
			row.files = type.files;
			rows->push_back(row);

			BString childPrefix(prefix);
			childPrefix << (last ? "    " : "\xe2\x94\x82   ");
			for (size_t child = 0; child < type.children.size(); child++)
				Collect(type.children[child], childPrefix, child + 1 == type.children.size());
		}
	} collector{types, &rows};
	for (size_t root = 0; root < roots.size(); root++)
		collector.Collect(roots[root], BString(), root + 1 == roots.size());

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
	printf("%s\n", VersionLine().String());
	printf("Server     %s\n", be_roster->IsRunning(sen::kServerSignature) ? "running" : "not running");

	std::vector<VolumeInfo> volumes = QueryableVolumes();
	int32 objects = 0;
	BString perVolume;
	BString notCounted;
	for (const VolumeInfo& volume : volumes) {
		if (!volume.hasIndexOf(sen::attr::kId)) {
			if (!notCounted.IsEmpty())
				notCounted << ", ";
			notCounted << volume.name;
			continue;
		}
		int32 count = CountEntries(volume, (BString(sen::attr::kId) << "==*").String());
		objects += count;
		if (count > 0) {
			if (!perVolume.IsEmpty())
				perVolume << ", ";
			perVolume << volume.name << " " << count;
		}
	}

	printf("Objects    %d", (int)objects);
	if (!perVolume.IsEmpty())
		printf("  (%s)", perVolume.String());
	printf("\n");
	if (!notCounted.IsEmpty())
		printf("           not counted, no index for %s (see create-sen-indices.sh): %s\n", sen::attr::kId, notCounted.String());

	printf("Types      %d entity \xc2\xb7 %d meta\n", (int)CountInstalledTypes(sen::mime::kEntitySupertype),
		(int)CountInstalledTypes(sen::mime::kMetaSupertype));
	printf("Relations  %d\n", (int)CountInstalledTypes(sen::mime::kRelationSupertype));
	return 0;
}

int
Types()
{
	std::vector<VolumeInfo> volumes = QueryableVolumes();
	const char* supertypes[] = {sen::mime::kMetaSupertype, sen::mime::kEntitySupertype, sen::mime::kRelationSupertype};

	std::vector<std::vector<TypeInfo> > all;
	std::vector<TypeInfo> toCount;
	for (const char* supertype : supertypes) {
		all.push_back(InstalledTypes(supertype));
		if (strcmp(supertype, sen::mime::kRelationSupertype) != 0)
			toCount.insert(toCount.end(), all.back().begin(), all.back().end());
	}

	Counts counts;
	CountFiles(volumes, toCount, &counts);

	for (size_t group = 0; group < all.size(); group++) {
		bool relations = strcmp(supertypes[group], sen::mime::kRelationSupertype) == 0;
		for (TypeInfo& type : all[group]) {
			if (relations) {
				// (SEN:REL:<the name after the supertype>)
				const char* subtype = strchr(type.type.c_str(), '/');
				type.files = counts.relationFiles[subtype != NULL ? subtype + 1 : type.type];
			} else
				type.files = counts.typeFiles[type.type];
		}

		if (group > 0)
			printf("\n");
		if (all[group].empty())
			printf("%s  no types installed\n", supertypes[group]);
		else
			PrintTypeTree(supertypes[group], all[group]);
	}

	if (!counts.notes.empty())
		printf("\n");
	for (const BString& name : counts.notes)
		printf("Volume %s cannot query the type of files: only its files with a SEN:ID are counted.\n", name.String());
	return 0;
}

int
Check()
{
	std::vector<VolumeInfo> volumes = QueryableVolumes();
	int problems = 0;

	// every file with a SEN:ID, on every volume: its ID (for duplicates) and its type (is it installed?)
	std::map<std::string, std::vector<BString> > idToPaths;
	std::map<std::string, std::vector<BString> > missingTypes;	// not installed type -> a few examples
	std::map<std::string, int32> missingCounts;
	std::map<std::string, bool> installedCache;
	int32 objects = 0;

	for (const VolumeInfo& volume : volumes) {
		if (!volume.hasIndexOf(sen::attr::kId)) {
			printf("PROBLEM: volume %s has no index for %s (SEN cannot find files by their ID there; run create-sen-indices.sh)\n",
				volume.name.String(), sen::attr::kId);
			problems++;
			continue;
		}

		bool ok = QueryVolume(volume, (BString(sen::attr::kId) << "==*").String(), [&](const entry_ref& ref) {
			BNode node(&ref);
			BString id;
			if (node.InitCheck() != B_OK || node.ReadAttrString(sen::attr::kId, &id) != B_OK || id.IsEmpty())
				return;
			objects++;
			idToPaths[id.String()].push_back(PathOf(ref));

			char type[B_MIME_TYPE_LENGTH];
			if (BNodeInfo(&node).GetType(type) != B_OK || type[0] == '\0')
				return;
			auto cached = installedCache.find(type);
			if (cached == installedCache.end())
				cached = installedCache.insert(std::make_pair(std::string(type), BMimeType(type).IsInstalled())).first;
			if (!cached->second) {
				missingCounts[type]++;
				if (missingTypes[type].size() < 3)
					missingTypes[type].push_back(PathOf(ref));
			}
		});
		if (!ok) {
			printf("PROBLEM: the query for %s failed on volume %s\n", sen::attr::kId, volume.name.String());
			problems++;
		}
	}

	printf("Checked %s on %s.\n", Plural(objects, "SEN object", "SEN objects").String(),
		Plural((int32)volumes.size(), "volume", "volumes").String());

	// duplicates: the ID must be unique (copies that were not detected)
	int32 duplicates = 0;
	for (const auto& entry : idToPaths) {
		if (entry.second.size() < 2)
			continue;
		if (duplicates++ == 0)
			printf("PROBLEM: SEN:IDs that more than one file carries:\n");
		printf("  %s: %d files\n", entry.first.c_str(), (int)entry.second.size());
		for (const BString& path : entry.second)
			printf("    %s\n", path.String());
	}
	problems += duplicates;

	// a type that is not installed any more: the ontology is not (completely) installed
	for (const auto& entry : missingCounts) {
		printf("PROBLEM: %s of type %s, which is not installed (broken or missing ontology?)\n",
			Plural(entry.second, "file", "files").String(), entry.first.c_str());
		for (const BString& path : missingTypes[entry.first])
			printf("    e.g. %s\n", path.String());
		problems++;
	}

	if (problems == 0)
		printf("No problems found.\n");
	else
		printf("%s found.\n", Plural(problems, "problem", "problems").String());
	return problems == 0 ? 0 : 1;
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
