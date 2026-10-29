/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "SenStats.h"

#include <sen/Sen.h>
#include <sen/Sensei.h>
#include <sen/SenVersion.h>

#include "../relations/QueryUtil.h"

#include <string.h>
#include <time.h>

#include <algorithm>
#include <set>
#include <utility>

#include <AppFileInfo.h>
#include <File.h>
#include <Message.h>
#include <MimeType.h>
#include <Node.h>
#include <NodeInfo.h>
#include <OS.h>
#include <Path.h>
#include <Query.h>
#include <Roster.h>
#include <Volume.h>
#include <VolumeRoster.h>
#include <fs_index.h>

namespace sen {
namespace stats {

// ---- volumes and queries

bool
Volume::HasIndex(const char* attribute) const
{
	index_info info;
	return fs_stat_index(device, attribute, &info) == 0;
}


std::vector<Volume>
QueryableVolumes()
{
	std::vector<Volume> volumes;
	BVolumeRoster roster;
	BVolume volume;
	while (roster.GetNextVolume(&volume) == B_OK) {
		// (a read-only volume, e.g. the packagefs of /system, cannot be changed, so SEN does not work with it, see SenServer::WatchVolume)
		if (!volume.KnowsQuery() || !volume.KnowsAttr() || volume.IsReadOnly())
			continue;
		Volume info;
		info.device = volume.Device();
		char name[B_FILE_NAME_LENGTH];
		info.name = volume.GetName(name) == B_OK && name[0] != '\0' ? name : "(unnamed)";
		volumes.push_back(info);
	}
	return volumes;
}


bool
QueryVolume(const Volume& volume, const char* predicate, const std::function<void(const entry_ref&)>& each)
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
CountEntries(const Volume& volume, const char* predicate)
{
	int32 count = 0;
	QueryVolume(volume, predicate, [&count](const entry_ref&) { count++; });
	return count;
}


static BString
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
static bool
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


static bool
ReadVersion(version_info* version, time_t* modified)
{
	BPath path;
	if (!ExecutablePath(&path))
		return false;

	BFile file(path.Path(), B_READ_ONLY);
	BAppFileInfo appInfo(&file);
	if (file.InitCheck() != B_OK || appInfo.GetVersionInfo(version, B_APP_VERSION_KIND) != B_OK)
		return false;
	*modified = 0;
	file.GetModificationTime(modified);
	return true;
}


static BString
VersionNumber(const version_info& version)
{
	return sen::VersionText(version);	// 1.0.0-beta.4, as the About windows of the plugins show it
}


BString
VersionLine()
{
	version_info version;
	time_t modified;
	if (!ReadVersion(&version, &modified))
		return BString("SEN Server (unknown version)");

	BString line(version.short_info[0] != '\0' ? version.short_info : "SEN Server");
	line << " " << VersionNumber(version);

	char date[16] = "";
	struct tm tm;
	if (modified != 0 && localtime_r(&modified, &tm) != NULL)
		strftime(date, sizeof(date), "%Y-%m-%d", &tm);
	if (date[0] != '\0')
		line << " (" << date << ")";	// (of the build)
	return line;
}


BString
VersionOnly()
{
	version_info version;
	time_t modified;
	if (!ReadVersion(&version, &modified))
		return BString("unknown version");
	return VersionNumber(version);
}


// ---- plugins

std::vector<PluginInfo>
GatherPlugins()
{
	std::vector<PluginInfo> plugins;
	BString predicate;
	predicate << "BEOS:APP_SIG==" << sensei::kSignaturePattern;
	std::vector<entry_ref> candidates;
	if (sen::QueryAllVolumes(predicate.String(), &candidates) != B_OK)
		return plugins;

	std::set<std::string> seen;
	for (const entry_ref& candidate : candidates) {
		BNode node(&candidate);
		BString type;
		if (node.InitCheck() != B_OK || node.ReadAttrString(sen::attr::kType, &type) != B_OK || type != sen::mime::kPlugin)
			continue;

		PluginInfo plugin;
		plugin.ref = candidate;
		BFile file(&candidate, B_READ_ONLY);
		BAppFileInfo appInfo(&file);
		char signature[B_MIME_TYPE_LENGTH];
		if (appInfo.InitCheck() != B_OK || appInfo.GetSignature(signature) != B_OK || !seen.insert(signature).second)
			continue;
		plugin.signature = signature;

		version_info version;
		bool hasVersion = appInfo.GetVersionInfo(&version, B_APP_VERSION_KIND) == B_OK;
		plugin.name = hasVersion && version.short_info[0] != '\0' ? version.short_info : candidate.name;
		node.ReadAttrString("SEN:plugin:version", &plugin.version);
		if (plugin.version.IsEmpty() && hasVersion) {
			plugin.version << (int)version.major << "." << (int)version.middle << "." << (int)version.minor;
		}

		for (const char* feature : {sensei::feature::kExtract, sensei::feature::kEnrich, sensei::feature::kIdentify, sensei::feature::kNavigate,
				sensei::feature::kSearch}) {
			BString attribute;
			attribute << sensei::kFeatureAttrPrefix << ":" << feature;
			int32 flag = 0;
			attr_info info;
			if (node.GetAttrInfo(attribute.String(), &info) == B_OK && info.size >= 1 && info.size <= (off_t) sizeof(flag)
					&& node.ReadAttr(attribute.String(), info.type, 0, &flag, info.size) == info.size && flag != 0) {
				if (!plugin.features.IsEmpty())
					plugin.features << ", ";
				plugin.features << feature;
			}
		}
		plugins.push_back(plugin);
	}
	std::sort(plugins.begin(), plugins.end(), [](const PluginInfo& a, const PluginInfo& b) { return a.name.ICompare(b.name) < 0; });
	return plugins;
}


// ---- overview

static int32
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


Overview
GatherOverview()
{
	Overview overview;
	overview.version = VersionLine();
	overview.serverRunning = be_roster->IsRunning(sen::kServerSignature);
	overview.objects = 0;

	for (const Volume& volume : QueryableVolumes()) {
		VolumeObjects entry;
		entry.name = volume.name;
		entry.objects = 0;
		entry.indexed = volume.HasIndex(sen::attr::kId);
		if (entry.indexed) {
			entry.objects = CountEntries(volume, (BString(sen::attr::kId) << "==*").String());
			overview.objects += entry.objects;
		}
		overview.volumes.push_back(entry);
	}

	overview.entities = CountInstalledTypes(sen::mime::kEntitySupertype);
	overview.metaTypes = CountInstalledTypes(sen::mime::kMetaSupertype);
	overview.classifications = CountInstalledTypes(sen::mime::kClassificationSupertype);
	overview.relations = CountInstalledTypes(sen::mime::kRelationSupertype);
	return overview;
}


BString
TypesSummary(const Overview& overview)
{
	BString text;
	text << Plural(overview.entities, "entity", "entities") << ", " << Plural(overview.metaTypes, "meta type", "meta types") << ", "
		<< Plural(overview.classifications, "classification", "classifications");
	return text;
}


// ---- types

/** The installed types of a supertype, with their descriptive names (the counts are filled in later). */
static std::vector<TypeNode>
InstalledTypes(const char* supertype)
{
	std::vector<TypeNode> types;
	BMessage installed;
	if (BMimeType::GetInstalledTypes(supertype, &installed) != B_OK)
		return types;

	const char* type;
	for (int32 index = 0; installed.FindString("types", index, &type) == B_OK; index++) {
		TypeNode node;
		node.type = type;
		const char* slash = strchr(type, '/');
		node.subtype = slash != NULL ? slash + 1 : type;
		char shortName[B_MIME_TYPE_LENGTH];
		node.name = BMimeType(type).GetShortDescription(shortName) == B_OK && shortName[0] != '\0' ? shortName : node.subtype.String();
		node.files = 0;
		types.push_back(node);
	}
	std::sort(types.begin(), types.end(), [](const TypeNode& a, const TypeNode& b) { return a.type < b.type; });
	return types;
}


/** parents: the longest other type that the name continues with a dot */
static void
BuildForest(TypeGroup* group)
{
	std::vector<TypeNode>& types = group->types;
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
	for (size_t i = 0; i < types.size(); i++) {
		if (parent[i] < 0)
			group->roots.push_back(i);
		else
			types[parent[i]].children.push_back(i);
	}
}


std::vector<TypeGroup>
GatherTypes(std::vector<BString>* notes)
{
	const char* supertypes[] = {sen::mime::kMetaSupertype, sen::mime::kEntitySupertype, sen::mime::kClassificationSupertype,
		sen::mime::kRelationSupertype};

	std::vector<TypeGroup> groups;
	for (const char* supertype : supertypes) {
		TypeGroup group;
		group.supertype = supertype;
		group.types = InstalledTypes(supertype);
		group.files = 0;
		groups.push_back(group);
	}

	std::vector<Volume> volumes = QueryableVolumes();
	std::map<std::string, int32> relationFiles;		// SEN:REL:<name after the supertype> -> files that have it
	std::map<std::string, int32> typeFiles;

	for (const Volume& volume : volumes) {
		std::map<std::string, int32> scanned;	// the type of the files with a SEN:ID, in case the type cannot be queried

		if (volume.HasIndex(sen::attr::kId)) {
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
						relationFiles[name + 8]++;
				}
			});
		}

		// the type of files is indexed by the file system itself (BEOS:TYPE does not show up in the list of indices)
		bool queried = true;
		bool any = false;
		std::map<std::string, int32> queriedCounts;
		for (const TypeGroup& group : groups) {
			if (group.supertype == sen::mime::kRelationSupertype)
				continue;
			for (const TypeNode& node : group.types) {
				any = true;
				BString predicate("BEOS:TYPE==\"");
				predicate << node.type.c_str() << "\"";
				int32 files = 0;
				if (!QueryVolume(volume, predicate.String(), [&files](const entry_ref&) { files++; })) {
					queried = false;
					break;
				}
				queriedCounts[node.type] += files;
			}
			if (!queried)
				break;
		}
		if (queried || !any) {
			for (const auto& entry : queriedCounts)
				typeFiles[entry.first] += entry.second;
		} else {
			// (a file system that cannot query the type: only the files with a SEN:ID are counted)
			for (const auto& entry : scanned)
				typeFiles[entry.first] += entry.second;
			if (notes != NULL)
				notes->push_back(volume.name);
		}
	}

	for (TypeGroup& group : groups) {
		bool relations = group.supertype == sen::mime::kRelationSupertype;
		for (TypeNode& node : group.types) {
			node.files = relations ? relationFiles[node.subtype.String()] : typeFiles[node.type];
			group.files += node.files;
		}
		BuildForest(&group);
	}
	return groups;
}


// ---- check

CheckResult
RunCheck()
{
	CheckResult result;
	result.objects = 0;

	std::vector<Volume> volumes = QueryableVolumes();
	result.volumes = (int32)volumes.size();

	std::map<std::string, std::vector<BString> > idToPaths;
	std::map<std::string, std::vector<BString> > missingTypes;	// a type that is not installed -> a few examples
	std::map<std::string, int32> missingCounts;
	std::map<std::string, bool> installedCache;

	for (const Volume& volume : volumes) {
		if (!volume.HasIndex(sen::attr::kId)) {
			Finding finding;
			finding.summary << "Volume " << volume.name << " has no index for " << sen::attr::kId
				<< " (SEN cannot find files by their ID there; run create-sen-indices.sh)";
			result.findings.push_back(finding);
			continue;
		}

		bool ok = QueryVolume(volume, (BString(sen::attr::kId) << "==*").String(), [&](const entry_ref& ref) {
			BNode node(&ref);
			BString id;
			if (node.InitCheck() != B_OK || node.ReadAttrString(sen::attr::kId, &id) != B_OK || id.IsEmpty())
				return;
			result.objects++;
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
			Finding finding;
			finding.summary << "The query for " << sen::attr::kId << " failed on volume " << volume.name;
			result.findings.push_back(finding);
		}
	}

	// duplicates: the ID must be unique (copies that were not detected)
	for (const auto& entry : idToPaths) {
		if (entry.second.size() < 2)
			continue;
		Finding finding;
		finding.summary << "SEN:ID " << entry.first.c_str() << " carried by " << (int)entry.second.size() << " files";
		finding.details = entry.second;
		result.findings.push_back(finding);
	}

	// a type that is not installed any more: the ontology is not (completely) installed
	for (const auto& entry : missingCounts) {
		Finding finding;
		finding.summary << Plural(entry.second, "file", "files") << " of type " << entry.first.c_str()
			<< ", which is not installed (broken or missing ontology?)";
		for (const BString& path : missingTypes[entry.first]) {
			BString example("e.g. ");
			example << path;
			finding.details.push_back(example);
		}
		result.findings.push_back(finding);
	}

	return result;
}

}	// namespace stats
}	// namespace sen
