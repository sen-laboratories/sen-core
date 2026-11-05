/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Entry.h>
#include <Window.h>

#include "Worker.h"

#include <map>
#include <vector>
#include <string>

#include <sen/SenGraph.h>

class BButton;
class BCheckBox;
class BFilePanel;
class BMessageRunner;
class BQuery;
class BStringView;
class SenConfigHandler;

namespace sen {
class EntityCard;

namespace ui {

class GraphView;

/** to the window: centre the graph on a file (refs) */
static const uint32 kGraphFocus = 'GWfo';

/**
 * The window of the Graph item of the Deskbar menu (also the Knot with the SEN key): the objects of SEN as the nodes of a graph and their relations as its
 * edges, laid out by SenGraph.h. The objects are read by a worker (Worker.h, see graph::Collect()). A click selects an object (its relations are
 * emphasised), a double click opens it as Tracker does; the mouse wheel zooms, dragging moves, F fits the graph into the window. The graph can be
 * saved as SVG.
 */
class GraphWindow : public BWindow {
public:
					GraphWindow(SenConfigHandler* config);
	virtual			~GraphWindow();

	virtual void	DispatchMessage(BMessage* message, BHandler* handler);
	virtual void	MessageReceived(BMessage* message);
	virtual bool	QuitRequested();

private:
	/** @param live a refresh because the files changed: the zoom, the place and the selection stay, and nothing is said while it is read */
	void			_Start(bool collect, bool live = false);
	/** watch the files of the graph (their attributes and names) and the objects of SEN (a live query), so that the graph follows what changes on the disk */
	void			_Watch();
	void			_StopWatching();
	/** something that is in the graph, or is an object of SEN, changed: read the objects again after a moment of quiet (what is written in a burst is read once) */
	void			_Changed(const BMessage* message);
	void			_ScheduleLive(bigtime_t delay);
	void			_Done(BMessage* message);
	void			_Open(const BMessage* message);
	void			_ApplyFocus();
	void			_OpenRelations(const BMessage* message);
	void			_Save(const BMessage* message);
	/** the cards of what is under the mouse in the graph: on or off */
	void			_UpdatePreview();
	sen::EntityCard*	_LoadCard(const std::string& id) const;

	GraphView*		fView;
	BCheckBox*		fMeta;
	BCheckBox*		fUnconnected;
	BCheckBox*		fOntologies;
	BCheckBox*		fPreview;
	BCheckBox*		fDark;
	BButton*		fSaveSelection;
	SenConfigHandler*	fConfig;
	bool			fSelectionOnly;	///< what the file panel is for: the selection, not the view
	BButton*		fRefresh;
	BStringView*	fStatus;
	BFilePanel*		fSavePanel;

	Worker*							fWorker;
	bool							fWorking;
	sen::graph::Graph				fAll;		///< every object and relation that was read
	std::map<std::string, entry_ref>	fRefs;
	bool							fTruncated;
	int32							fObjects;
	bool							fLiveRun;	///< the work that is running (or was started last) is a live refresh
	bool							fCollecting;	///< ... and it reads the files again (not only a new layout)
	bool							fLiveAgain;	///< something changed while the objects were read: read them again
	BMessageRunner*					fLiveRunner;
	std::vector<BQuery*>			fQueries;	///< live queries for the objects of SEN, one for each volume that has them
	bool							fHasFocus;	///< a file to centre the graph on, as soon as the graph is there
	entry_ref						fFocusRef;
};

}	// namespace ui
}	// namespace sen
