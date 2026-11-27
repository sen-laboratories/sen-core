/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "SenIndices.h"

#include <sen/Sen.h>

#include "../relations/QueryUtil.h"

#include <dirent.h>
#include <errno.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <set>
#include <string>

#include <Directory.h>
#include <Entry.h>
#include <Node.h>
#include <Query.h>
#include <Volume.h>
#include <fs_attr.h>
#include <fs_index.h>

#include <spdlog/spdlog.h>

namespace sen {
namespace stats {

namespace {

const char kIndexPrefix[] = "SEN:";

struct IndexName {
	std::string	name;
	uint32		type;
};


/** The indices of SEN of a volume (fs_open_index_dir lists them all, the names of SEN start with "SEN:"). */
std::vector<IndexName>
SenIndicesOf(dev_t device)
{
	std::vector<IndexName> indices;
	DIR* directory = fs_open_index_dir(device);
	if (directory == NULL)
		return indices;
	while (struct dirent* entry = fs_read_index_dir(directory)) {
		if (strncmp(entry->d_name, kIndexPrefix, strlen(kIndexPrefix)) != 0)
			continue;
		index_info info;
		IndexName index;
		index.name = entry->d_name;
		index.type = fs_stat_index(device, entry->d_name, &info) == 0 ? info.type : 0;
		indices.push_back(index);
	}
	fs_close_index_dir(directory);
	return indices;
}


/** The files of the volume that have a SEN:ID (the SEN objects): the ones that carry SEN's attributes. */
void
ForEachObject(const Volume& volume, int32* unreadable, const std::function<void(const entry_ref&)>& each)
{
	BVolume bvolume(volume.device);
	BQuery query;
	BString predicate(sen::attr::kId);
	predicate << "==*";
	if (query.SetVolume(&bvolume) != B_OK || query.SetPredicate(predicate.String()) != B_OK || query.Fetch() != B_OK)
		return;
	ReadQuery(query, each, unreadable);
}


}	// namespace


BString
IndexState::Describe() const
{
	if (!Damaged())
		return BString();

	BString text(attribute);
	text << " on " << volume << ": ";
	bool first = true;
	if (missing > 0) {
		text << "the query finds " << found - stale << " of the " << Plural(expected, "file", "files") << " that have the attribute";
		first = false;
	}
	if (stale > 0) {
		text << (first ? "" : ", ") << Plural(stale, "entry", "entries") << " of files that do not have it (any more)";
		first = false;
	}
	if (unreadable > 0)
		text << (first ? "" : ", ") << Plural(unreadable, "entry", "entries") << " that the query cannot read (deleted files)";
	return text;
}


std::vector<IndexState>
CheckIndices(const Volume& volume)
{
	std::vector<IndexState> states;
	std::vector<IndexName> indices = SenIndicesOf(volume.device);
	if (indices.empty())
		return states;

	bool hasIdIndex = false;
	for (const IndexName& index : indices)
		hasIdIndex = hasIdIndex || index.name == sen::attr::kId;

	// the objects, and which of the indexed attributes each of them carries: what an index has to find
	std::map<std::string, std::set<ino_t> > expected;
	std::map<std::string, bool> indexed;
	for (const IndexName& index : indices)
		indexed[index.name] = true;

	int32 objectsUnreadable = 0;
	if (hasIdIndex) {
		ForEachObject(volume, &objectsUnreadable, [&](const entry_ref& ref) {
			BNode node(&ref);
			node_ref nodeRef;
			if (node.InitCheck() != B_OK || node.GetNodeRef(&nodeRef) != B_OK)
				return;
			char name[B_ATTR_NAME_LENGTH];
			node.RewindAttrs();
			while (node.GetNextAttrName(name) == B_OK) {
				if (indexed.count(name) != 0)
					expected[name].insert(nodeRef.node);
			}
		});
	}

	for (const IndexName& index : indices) {
		IndexState state;
		state.device = volume.device;
		state.volume = volume.name;
		state.attribute = index.name.c_str();
		state.type = index.type;
		state.compared = hasIdIndex && index.type == B_STRING_TYPE;
		state.expected = (int32)expected[index.name].size();
		state.found = 0;
		state.missing = 0;
		state.stale = 0;
		state.unreadable = 0;

		// (only a string index answers "attribute==*": the others are not compared, nor queried)
		if (index.type == B_STRING_TYPE) {
			BVolume bvolume(volume.device);
			BQuery query;
			BString predicate(index.name.c_str());
			predicate << "==*";
			if (query.SetVolume(&bvolume) == B_OK && query.SetPredicate(predicate.String()) == B_OK && query.Fetch() == B_OK) {
				std::set<ino_t> seen;
				ReadQuery(query, [&](const entry_ref& ref) {
					BNode node(&ref);
					node_ref nodeRef;
					attr_info info;
					if (node.InitCheck() != B_OK || node.GetNodeRef(&nodeRef) != B_OK)
						return;
					seen.insert(nodeRef.node);
					state.found++;
					if (node.GetAttrInfo(index.name.c_str(), &info) != B_OK)
						state.stale++;
				}, &state.unreadable);

				if (state.compared) {
					for (ino_t node : expected[index.name]) {
						if (seen.count(node) == 0)
							state.missing++;
					}
				}
			}
		}
		states.push_back(state);
	}
	return states;
}


BString
RepairResult::Report() const
{
	BString text;
	for (const BString& note : notes)
		text << note << "\n";
	return text;
}


namespace {

std::atomic<bool> sRepairing(false);


/**
 * Write the attributes of a node that are in @p names again, the way `reindex` does: read the value, REMOVE the attribute, write the value. Writing a value over the same value
 * leaves the index as it is (BFS finds nothing to change), and the new index stays empty: the attribute has to go and come again for the file to get into the index.
 * An attribute that cannot be written again is retried once and then counted in @p lost (its value is in the log: it must not be lost silently).
 */
int32
RewriteAttributes(BNode* node, const std::set<std::string>& names, int32* lost, const char* path)
{
	int32 rewritten = 0;
	for (const std::string& name : names) {
		attr_info info;
		if (node->GetAttrInfo(name.c_str(), &info) != B_OK || info.size < 0)
			continue;
		std::vector<char> value((size_t)info.size + 1);
		ssize_t size = info.size > 0 ? node->ReadAttr(name.c_str(), info.type, 0, value.data(), (size_t)info.size) : 0;
		if (size != info.size)
			continue;
		if (node->RemoveAttr(name.c_str()) != B_OK)
			continue;
		ssize_t written = node->WriteAttr(name.c_str(), info.type, 0, value.data(), (size_t)info.size);
		if (written != info.size)
			written = node->WriteAttr(name.c_str(), info.type, 0, value.data(), (size_t)info.size);
		if (written == info.size)
			rewritten++;
		else {
			(*lost)++;
			spdlog::error("repair indices: could not write the attribute {} of {} again, its value was: {}", name, path,
				std::string(value.data(), (size_t)info.size));
		}
	}
	return rewritten;
}


struct Rewriter {
	dev_t							device;
	std::set<std::string>			names;
	RepairResult*					result;
	const std::function<void(const BString&)>&	progress;
	BString							volume;

	void File(const entry_ref& ref)
	{
		BNode node(&ref);
		if (node.InitCheck() != B_OK)
			return;
		_Node(&node, ref.name);
	}

	/** a file or folder of the volume, as the entry that the query found */
	void Entry(const entry_ref& ref) { File(ref); }

private:
	void _Node(BNode* node, const char* path)
	{
		result->filesScanned++;
		result->attributesRewritten += RewriteAttributes(node, names, &result->attributesLost, path);
		if (progress && result->filesScanned % 2000 == 0) {
			BString line;
			line << volume << ": " << result->filesScanned << " files looked at" << B_UTF8_ELLIPSIS;
			progress(line);
		}
	}
};

}	// namespace


status_t
RepairIndices(RepairResult* resultOrNull, bool force, bool deep, const std::function<void(const BString&)>& progress)
{
	RepairResult local;
	RepairResult* result = resultOrNull != NULL ? resultOrNull : &local;
	if (sRepairing.exchange(true)) {
		result->notes.push_back(BString("A repair is running already."));
		return B_BUSY;
	}

	auto note = [&](const BString& line) {
		result->notes.push_back(line);
		spdlog::info("repair indices: {}", line.String());
		if (progress)
			progress(line);
	};

	std::vector<Volume> volumes = QueryableVolumes();
	for (const Volume& volume : volumes) {
		result->volumes++;

		if (SenIndicesOf(volume.device).empty()) {
			BString line("Volume ");
			line << volume.name << ": no indices of SEN (create-sen-indices.sh makes them)";
			note(line);
			continue;
		}

		std::vector<IndexState> states = CheckIndices(volume);
		std::vector<IndexState> repair;
		bool idDamaged = false;
		for (const IndexState& state : states) {
			if (state.Damaged()) {
				result->indicesDamaged++;
				note(BString("Damaged: ") << state.Describe());
			}
			if (force || state.Damaged())
				repair.push_back(state);
			idDamaged = idDamaged || (state.attribute == sen::attr::kId && state.Damaged());
		}
		if (force)
			result->indicesDamaged = std::max(result->indicesDamaged, (int32)repair.size());

		if (repair.empty()) {
			BString line("Volume ");
			line << volume.name << ": " << Plural((int32)states.size(), "index", "indices") << " of SEN, all in order";
			note(line);
			continue;
		}

		// the files to write: the SEN objects, found through the index of SEN:ID BEFORE any index is removed (the query for the names finds all files without it: also needed when that index is damaged)
		bool needsAll = deep || idDamaged;
		std::vector<entry_ref> objects;
		if (!needsAll) {
			int32 unreadable = 0;
			ForEachObject(volume, &unreadable, [&](const entry_ref& ref) { objects.push_back(ref); });
		}

		// remove and create again, with the type that the index had
		Rewriter rewriter{volume.device, {}, result, progress, volume.name};
		std::vector<IndexState> made;
		for (const IndexState& state : repair) {
			if (fs_remove_index(volume.device, state.attribute.String()) != 0 && errno != B_ENTRY_NOT_FOUND) {
				BString line("Cannot remove the index ");
				line << state.attribute << " of " << volume.name << ": " << strerror(errno);
				note(line);
				result->failures++;
				continue;
			}
			if (fs_create_index(volume.device, state.attribute.String(), state.type != 0 ? state.type : B_STRING_TYPE, 0) != 0) {
				BString line("Cannot create the index ");
				line << state.attribute << " of " << volume.name << " again: " << strerror(errno);
				note(line);
				result->failures++;
				continue;
			}
			rewriter.names.insert(state.attribute.String());
			made.push_back(state);
		}
		if (made.empty())
			continue;

		// the files: all of the volume, or the SEN objects
		BString line("Volume ");
		line << volume.name << ": " << Plural((int32)made.size(), "index", "indices") << " made again, writing the attributes of the "
			<< (needsAll ? "files" : "SEN objects") << " again" << B_UTF8_ELLIPSIS;
		note(line);
		if (needsAll) {
			// every file of the volume: the query for the name index (which BFS keeps itself) lists them all. A walk from the root would miss the files in the folders that shine
			// through a packagefs mount (~/config/settings: the folders of the contexts are there), as the mount has another device than the volume.
			BVolume bvolume(volume.device);
			BQuery everything;
			if (everything.SetVolume(&bvolume) == B_OK && everything.SetPredicate("name=*") == B_OK && everything.Fetch() == B_OK)
				ReadQuery(everything, [&](const entry_ref& ref) { rewriter.Entry(ref); });
			else {
				note(BString("Cannot list the files of ") << volume.name << " (the query for their names failed)");
				result->failures++;
			}
		} else {
			for (const entry_ref& ref : objects)
				rewriter.File(ref);
		}

		// did it work? (an index that is in order has all objects that carry the attribute and no stale entry)
		std::vector<IndexState> after = CheckIndices(volume);
		for (const IndexState& state : after) {
			bool wasMade = false;
			for (const IndexState& m : made)
				wasMade = wasMade || m.attribute == state.attribute;
			if (!wasMade)
				continue;
			if (state.Damaged()) {
				result->failures++;
				note(BString("Still damaged: ") << state.Describe());
			} else
				result->indicesRepaired++;
		}
	}

	if (result->attributesLost > 0) {
		result->failures += result->attributesLost;
		note(BString("Attributes that could not be written again: ") << result->attributesLost << " (their values are in the log of the server)");
	}

	BString summary;
	if (result->indicesDamaged == 0 && !force)
		summary << "All indices of SEN are in order.";
	else {
		summary << Plural(result->indicesRepaired, "index", "indices") << " made again";
		if (result->failures > 0)
			summary << ", " << Plural(result->failures, "problem", "problems") << " remain";
		summary << " (" << Plural(result->attributesRewritten, "attribute", "attributes") << " written again in "
			<< Plural(result->filesScanned, "file", "files") << ").";
	}
	note(summary);

	sRepairing = false;
	return result->failures == 0 ? B_OK : B_ERROR;
}

}	// namespace stats
}	// namespace sen
