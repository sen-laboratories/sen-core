/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

/**
 * @file SenStats.h
 * @brief What SEN knows about itself, as data: the overview, the installed types with the number of files, and the consistency check.
 *
 * Shared by the command line of the server (--info, --types, --check, see Cli.h) and the windows of the Deskbar menu. Everything here asks the
 * file system (queries over all volumes that SEN works with) and the MIME database; none of it needs the server to run.
 */

#include <Entry.h>
#include <String.h>
#include <SupportDefs.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace sen {
namespace stats {

/** A volume that SEN works with: it knows queries and attributes, and can be changed (not the packagefs). */
struct Volume {
	dev_t		device;
	BString		name;

	/** Whether the volume has an index for the attribute (the type, BEOS:TYPE, is indexed by the file system itself and not listed). */
	bool		HasIndex(const char* attribute) const;
};

std::vector<Volume> QueryableVolumes();

/** Run a query on a volume and call @p each(entry_ref) with every entry found; false if the query failed (e.g. no index). */
bool QueryVolume(const Volume& volume, const char* predicate, const std::function<void(const entry_ref&)>& each);

/** The number of entries that the query finds on the volume. */
int32 CountEntries(const Volume& volume, const char* predicate);

/** "SEN Server 1.0.0 development (2026-10-11, build 0)": the name and version of the application resource, the day the program was made. */
BString VersionLine();
/** The same without the date and build: "1.0.0 development". */
BString VersionOnly();

// ---- overview

struct VolumeObjects {
	BString		name;
	int32		objects;
	bool		indexed;	///< false: not counted, there is no index for SEN:ID
};

struct Overview {
	BString						version;
	bool						serverRunning;
	int32						objects;			///< files with a SEN:ID, on all volumes
	std::vector<VolumeObjects>	volumes;
	int32						entities;			///< installed types: entity/*
	int32						metaTypes;			///< meta/*
	int32						classifications;	///< classification/*
	int32						relations;			///< relation/*
};

Overview GatherOverview();

/** "13 entities, 5 meta types, 3 classifications": the types of an overview in plural, as a line. */
BString TypesSummary(const Overview& overview);
/** "1 type" / "12 types", "1 file" / "2 files"... */
BString Plural(int32 count, const char* singular, const char* plural);

// ---- plugins

/** A SEN plugin that is installed (an application with a signature of sensei::kSignaturePattern and the plugin type). */
struct PluginInfo {
	BString		name;			///< as people read it: the short info of its version resource, else the name of the file
	BString		signature;
	BString		version;		///< its attribute SEN:plugin:version, else the version of the application resource
	BString		features;		///< "extract, identify"
	entry_ref	ref;
};

/** The installed plugins of all volumes (also those of packages), sorted by name. */
std::vector<PluginInfo> GatherPlugins();

// ---- types

struct TypeNode {
	std::string			type;		///< the MIME type, e.g. "entity/x-vnd.sen-labs.entity.movie"
	BString				name;		///< as people read it (the short description), else the subtype
	BString				subtype;	///< the type without the supertype
	int32				files;
	std::vector<size_t>	children;	///< indices in TypeGroup::types: the types that continue this name with a dot
};

/** The installed types of a supertype as a forest: a type whose name continues another one with a dot (the movie medium of a movie) is its child. */
struct TypeGroup {
	BString					supertype;
	std::vector<TypeNode>	types;
	std::vector<size_t>		roots;
	int32					files;		///< the sum of the files of the types
};

/**
 * The groups of the types that SEN installs: meta, entity, classification and relation. The files of a type are queried by its type
 * (BEOS:TYPE); for relations they are the files that have such a relation. Volumes that cannot query the type are named in @p notes
 * (their files with a SEN:ID are counted instead).
 */
std::vector<TypeGroup> GatherTypes(std::vector<BString>* notes);

// ---- check

/** One kind of problem with what it affects. */
struct Finding {
	BString					summary;
	std::vector<BString>	details;
};

struct CheckResult {
	int32					objects;	///< SEN objects checked
	int32					volumes;
	std::vector<Finding>	findings;
};

/**
 * Look for inconsistencies: volumes without an index for SEN:ID, SEN:IDs that more than one file carries, and files with a SEN:ID whose
 * type is not installed (a broken or missing ontology).
 */
CheckResult RunCheck();

}	// namespace stats
}	// namespace sen
