/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "GraphWindow.h"

#include "../graph/GraphData.h"

#include <Alert.h>
#include <Button.h>
#include <CheckBox.h>
#include <Directory.h>
#include <File.h>
#include <FilePanel.h>
#include <Font.h>
#include <LayoutBuilder.h>
#include <Messenger.h>
#include <NodeInfo.h>
#include <ScrollBar.h>
#include <StringView.h>
#include <View.h>

#include <math.h>
#include <string.h>

#include <algorithm>
#include <vector>

#include <sen/Sen.h>

namespace sen {
namespace ui {

using sen::graph::Edge;
using sen::graph::Node;

static const uint32 kRefresh = 'GWrf';
static const uint32 kFilter = 'GWfl';
static const uint32 kSave = 'GWsv';
static const uint32 kDark = 'GWdk';
static const uint32 kWorkDone = 'GWdn';
static const uint32 kOpenNode = 'GWop';
static const uint32 kOpenRelations = 'GWor';
static const uint32 kSelectNode = 'GWsl';
static const uint32 kActivate = 'GWac';

/** the most objects that are shown (the layout and the drawing are made for some thousands, not more) */
static const size_t kMaxObjects = 2000;

static const char* const kTrackerSignature = "application/x-vnd.Be-TRAK";


static rgb_color
Color(const sen::graph::Rgb& color, uint8 alpha = 255)
{
	rgb_color result = {color.r, color.g, color.b, alpha};
	return result;
}


/** whether the colours of the system are dark (the background of documents) */
static bool
SystemIsDark()
{
	const rgb_color background = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
	return (background.red * 299 + background.green * 587 + background.blue * 114) / 1000 < 128;
}


static BString
EdgeLabel(const Edge& edge)
{
	BString label(edge.label.c_str());
	if (edge.reciprocal && !edge.inverseLabel.empty() && edge.inverseLabel != edge.label) {
		if (!label.IsEmpty())
			label << " / ";
		label << edge.inverseLabel.c_str();
	}
	return label;
}


/** the distance of a point from the line between two points (not beyond its ends) */
static float
DistanceToSegment(BPoint point, BPoint a, BPoint b)
{
	const float dx = b.x - a.x, dy = b.y - a.y;
	const float length2 = dx * dx + dy * dy;
	if (length2 < 0.0001f)
		return hypotf(point.x - a.x, point.y - a.y);
	float t = ((point.x - a.x) * dx + (point.y - a.y) * dy) / length2;
	t = std::min(std::max(t, 0.0f), 1.0f);
	return hypotf(point.x - (a.x + t * dx), point.y - (a.y + t * dy));
}


/** the name of a relation type for people, as far as it is in the name: "relation/x-vnd.sen-labs.relation.person.knows" is "person.knows" */
static BString
ShortTypeName(const std::string& type)
{
	BString name(type.c_str());
	name.RemoveFirst("relation/");
	name.RemoveFirst("x-vnd.sen-labs.relation.");
	return name;
}


// ---- the view

/** The graph, drawn: moved by dragging, zoomed with the wheel, a click selects a node, a double click asks to open it. */
class GraphView : public BView {
public:
	GraphView()
		:
		BView("graph", B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE | B_NAVIGABLE),
		fScale(1),
		fOffset(0, 0),
		fNeedFit(true),
		fHover(-1),
		fSelected(-1),
		fHoverEdge(-1),
		fSelectedEdge(-1),
		fDragging(false),
		fDark(SystemIsDark())
	{
		SetViewColor(B_TRANSPARENT_COLOR);
		SetExplicitMinSize(BSize(400, 300));
	}

	void SetGraph(const sen::graph::Graph& graph)
	{
		fGraph = graph;
		fHover = fSelected = fHoverEdge = fSelectedEdge = -1;
		fNeighbors.assign(fGraph.Nodes().size(), false);
		fNeedFit = true;
		Invalidate();
	}

	void SetMessage(const char* text)
	{
		fMessage = text;
		Invalidate();
	}

	const sen::graph::Graph& Graph() const { return fGraph; }

	bool Dark() const { return fDark; }

	/** select the node of an id (what a click does) */
	void SelectId(const char* id)
	{
		_Select(fGraph.IndexOf(id));
	}

	void SetDark(bool dark)
	{
		fDark = dark;
		Invalidate();
	}

	virtual void AttachedToWindow()
	{
		BView::AttachedToWindow();
		SetFlags(Flags() | B_SUBPIXEL_PRECISE);
		MakeFocus(true);
	}

	virtual void Draw(BRect updateRect);

	virtual void MouseDown(BPoint where)
	{
		MakeFocus(true);
		BMessage* current = Window()->CurrentMessage();
		uint32 buttons = current->GetInt32("buttons", 0);
		if ((buttons & B_PRIMARY_MOUSE_BUTTON) == 0)
			return;

		const bool doubleClick = current->GetInt32("clicks", 1) >= 2;
		int32 node = _NodeAt(where);
		bool nearFrom = true;
		int32 edge = node < 0 ? _EdgeAt(where, &nearFrom) : -1;
		if (node >= 0) {
			_Select(node);
			if (doubleClick) {
				BMessage open(kOpenNode);
				open.AddString("id", fGraph.Nodes()[node].id.c_str());
				Window()->PostMessage(&open);
			}
		} else if (edge >= 0) {
			_SelectEdge(edge);
			if (doubleClick) {
				// a relation is in both nodes (reciprocal): the relations of its type of the node that is nearer to the click
				const Edge& selected = fGraph.Edges()[edge];
				BMessage open(kOpenRelations);
				open.AddString("id", fGraph.Nodes()[nearFrom ? selected.from : selected.to].id.c_str());
				open.AddString("type", selected.type.c_str());
				Window()->PostMessage(&open);
			}
		} else
			_Select(-1);
		fDragging = true;
		fLast = where;
		SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
	}

	virtual void MouseUp(BPoint)
	{
		fDragging = false;
	}

	virtual void MouseMoved(BPoint where, uint32, const BMessage*)
	{
		if (fDragging) {
			fOffset.x += where.x - fLast.x;
			fOffset.y += where.y - fLast.y;
			fLast = where;
			Invalidate();
			return;
		}
		int32 node = _NodeAt(where);
		bool nearFrom;
		int32 edge = node < 0 ? _EdgeAt(where, &nearFrom) : -1;
		if (node != fHover || edge != fHoverEdge || node >= 0 || edge >= 0) {
			fHover = node;
			fHoverEdge = edge;
			fMouse = where;
			Invalidate();
		}
	}

	virtual void MessageReceived(BMessage* message)
	{
		float delta;
		if (message->what == B_MOUSE_WHEEL_CHANGED && message->FindFloat("be:wheel_delta_y", &delta) == B_OK) {
			BPoint where;
			uint32 buttons;
			GetMouse(&where, &buttons, false);
			_Zoom(where, powf(1.15f, -delta));
			return;
		}
		BView::MessageReceived(message);
	}

	virtual void KeyDown(const char* bytes, int32 count)
	{
		if (count != 1) {
			BView::KeyDown(bytes, count);
			return;
		}
		switch (bytes[0]) {
			case 'f':
			case 'F':
				fNeedFit = true;
				Invalidate();
				break;
			case '+':
			case '=':
				_Zoom(BPoint(Bounds().Width() / 2, Bounds().Height() / 2), 1.25f);
				break;
			case '-':
				_Zoom(BPoint(Bounds().Width() / 2, Bounds().Height() / 2), 0.8f);
				break;
			default:
				BView::KeyDown(bytes, count);
		}
	}

private:
	BPoint _ToView(const Node& node) const
	{
		return BPoint(node.x * fScale + fOffset.x, node.y * fScale + fOffset.y);
	}

	/** the size of a node on the screen: the zoom does not make the nodes as big as the distances */
	float _Radius(const Node& node) const
	{
		return sen::graph::NodeRadius(node.degree) * std::min(std::max(fScale, 0.7f), 1.6f);
	}

	int32 _NodeAt(BPoint where) const
	{
		const std::vector<Node>& nodes = fGraph.Nodes();
		int32 found = -1;
		float best = 1e9f;
		for (size_t i = 0; i < nodes.size(); i++) {
			BPoint at = _ToView(nodes[i]);
			float distance = hypotf(at.x - where.x, at.y - where.y);
			if (distance <= _Radius(nodes[i]) + 3 && distance < best) {
				best = distance;
				found = static_cast<int32>(i);
			}
		}
		return found;
	}

	/** the line under a point (not at a node), and whether the point is nearer to the first of its nodes */
	int32 _EdgeAt(BPoint where, bool* nearFrom) const
	{
		const std::vector<Node>& nodes = fGraph.Nodes();
		const std::vector<Edge>& edges = fGraph.Edges();
		int32 found = -1;
		float best = 6.0f;	// the pixels around a line that are the line
		for (size_t i = 0; i < edges.size(); i++) {
			const BPoint a = _ToView(nodes[edges[i].from]), b = _ToView(nodes[edges[i].to]);
			const float distance = DistanceToSegment(where, a, b);
			if (distance < best) {
				best = distance;
				found = static_cast<int32>(i);
				*nearFrom = hypotf(where.x - a.x, where.y - a.y) <= hypotf(where.x - b.x, where.y - b.y);
			}
		}
		return found;
	}

	void _SelectEdge(int32 edge)
	{
		fSelected = -1;
		fSelectedEdge = edge;
		fNeighbors.assign(fGraph.Nodes().size(), false);
		if (edge >= 0) {
			fNeighbors[fGraph.Edges()[edge].from] = true;
			fNeighbors[fGraph.Edges()[edge].to] = true;
		}
		Invalidate();
	}

	void _Select(int32 node)
	{
		fSelectedEdge = -1;
		fSelected = node;
		fNeighbors.assign(fGraph.Nodes().size(), false);
		if (node >= 0) {
			for (const Edge& edge : fGraph.Edges()) {
				if (edge.from == node)
					fNeighbors[edge.to] = true;
				else if (edge.to == node)
					fNeighbors[edge.from] = true;
			}
		}
		Invalidate();
	}

	void _Zoom(BPoint where, float factor)
	{
		float scale = std::min(std::max(fScale * factor, 0.05f), 6.0f);
		factor = scale / fScale;
		fOffset.x = where.x - (where.x - fOffset.x) * factor;
		fOffset.y = where.y - (where.y - fOffset.y) * factor;
		fScale = scale;
		Invalidate();
	}

	void _Fit()
	{
		float left, top, right, bottom;
		fGraph.Bounds(&left, &top, &right, &bottom);
		const float margin = 50;
		BRect bounds(Bounds());
		float width = std::max(right - left, 1.0f), height = std::max(bottom - top, 1.0f);
		fScale = std::min(std::max(bounds.Width() - 2 * margin, 50.0f) / width, std::max(bounds.Height() - 2 * margin, 50.0f) / height);
		fScale = std::min(fScale, 2.5f);
		fOffset.x = bounds.Width() / 2 - (left + width / 2) * fScale;
		fOffset.y = bounds.Height() / 2 - (top + height / 2) * fScale;
		fNeedFit = false;
	}

	sen::graph::Graph	fGraph;
	std::vector<bool>	fNeighbors;		///< the nodes that the selected one is connected to
	BString				fMessage;
	float				fScale;
	BPoint				fOffset;
	bool				fNeedFit;
	int32				fHover;
	int32				fSelected;
	int32				fHoverEdge;
	int32				fSelectedEdge;
	BPoint				fMouse;
	BPoint				fLast;
	bool				fDragging;
	bool				fDark;
};


void
GraphView::Draw(BRect)
{
	const sen::graph::Palette palette = sen::graph::PaletteFor(fDark);
	const rgb_color background = Color(palette.background);
	const rgb_color textColor = Color(palette.text);
	SetLowColor(background);
	FillRect(Bounds(), B_SOLID_LOW);

	BFont font(be_plain_font);
	SetFont(&font);
	font_height height;
	font.GetHeight(&height);

	const std::vector<Node>& nodes = fGraph.Nodes();
	if (nodes.empty()) {
		SetHighColor(Color(palette.edgeText));
		BRect bounds(Bounds());
		BString text(fMessage.IsEmpty() ? "No objects with relations" : fMessage.String());
		DrawString(text.String(), BPoint((bounds.Width() - font.StringWidth(text.String())) / 2, bounds.Height() / 2));
		return;
	}

	if (fNeedFit)
		_Fit();

	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);

	const bool selectingNode = fSelected >= 0, selectingEdge = fSelectedEdge >= 0;
	const bool selecting = selectingNode || selectingEdge;
	const bool manyNodes = nodes.size() > 60;
	const std::vector<Edge>& edges = fGraph.Edges();

	// is a line one of those that are emphasised: the ones of the selected node, the selected line, the one under the mouse
	auto emphasised = [&](size_t index) {
		const Edge& edge = edges[index];
		return (selectingNode && (edge.from == fSelected || edge.to == fSelected)) || static_cast<int32>(index) == fSelectedEdge
			|| static_cast<int32>(index) == fHoverEdge;
	};

	// the edges: a line to the rim of the node, an arrow if the relation is in one direction only
	for (size_t index = 0; index < edges.size(); index++) {
		const Edge& edge = edges[index];
		const bool strong = emphasised(index);
		const bool active = !selecting || strong;
		const Node& from = nodes[edge.from];
		const Node& to = nodes[edge.to];
		BPoint a = _ToView(from), b = _ToView(to);
		float dx = b.x - a.x, dy = b.y - a.y;
		float length = std::max(hypotf(dx, dy), 0.01f);
		dx /= length;
		dy /= length;
		const float startRadius = _Radius(from), endRadius = _Radius(to) + (edge.reciprocal ? 0 : 1);
		BPoint start(a.x + dx * startRadius, a.y + dy * startRadius), end(b.x - dx * endRadius, b.y - dy * endRadius);

		rgb_color color = Color(edge.meta ? palette.metaEdge : palette.edge);
		if (strong)
			color = Color(palette.emphasis);
		color.alpha = active ? 255 : 50;
		SetHighColor(color);
		SetPenSize(strong ? 2.4f : 1.2f);
		StrokeLine(start, end);

		if (!edge.reciprocal && length > startRadius + endRadius + 8) {
			const float size = 7;
			BPoint left(end.x - dx * size - dy * size / 2.5f, end.y - dy * size + dx * size / 2.5f);
			BPoint right(end.x - dx * size + dy * size / 2.5f, end.y - dy * size - dx * size / 2.5f);
			FillTriangle(end, left, right);
		}
	}

	// Which labels are drawn. The wanted ones are placed by their importance, and one that would lie on another that is placed is left out: the node that is
	// selected or under the mouse, the nodes it is connected to, the selected line, then the lines of the selected node (if it has not many), the hubs,
	// the rest.
	std::vector<bool> edgeLabelShown(edges.size(), false), nodeLabelShown(nodes.size(), false);
	std::vector<BString> nodeTexts(nodes.size()), edgeTexts(edges.size());
	{
		struct Candidate {
			bool	isNode;
			size_t	index;
			int		priority;
			BRect	rect;
		};
		std::vector<Candidate> candidates;
		const int selectedDegree = selectingNode ? nodes[fSelected].degree : 0;

		for (size_t i = 0; i < nodes.size(); i++) {
			const Node& node = nodes[i];
			const bool chosen = static_cast<int32>(i) == fSelected || static_cast<int32>(i) == fHover;
			const bool related = selecting && fNeighbors[i];
			bool wanted = chosen || related;
			if (!wanted && !selecting)
				wanted = !manyNodes || fScale >= 1.0f || (node.degree >= 3 && fScale >= 0.4f);
			if (!wanted)
				continue;
			BString label(node.label.c_str());
			if (label.CountChars() > 28)
				label.TruncateChars(27).Append(B_UTF8_ELLIPSIS);
			nodeTexts[i] = label;
			const BPoint at = _ToView(node);
			const float width = font.StringWidth(label.String());
			const BPoint where(at.x - width / 2, at.y + _Radius(node) + height.ascent + 2);
			Candidate candidate = {true, i, chosen ? 0 : (related ? 1 : (node.degree >= 3 ? 4 : 6)),
				BRect(where.x - 2, where.y - height.ascent, where.x + width + 2, where.y + height.descent)};
			candidates.push_back(candidate);
		}

		for (size_t index = 0; index < edges.size(); index++) {
			const Edge& edge = edges[index];
			const bool direct = static_cast<int32>(index) == fSelectedEdge || static_cast<int32>(index) == fHoverEdge;
			const bool ofSelected = selectingNode && (edge.from == fSelected || edge.to == fSelected) && selectedDegree <= 8;
			bool wanted = direct || ofSelected;
			if (!wanted && !selecting)
				wanted = !((manyNodes && fScale < 1.2f) || fScale < 0.7f);
			if (!wanted)
				continue;
			BString label = EdgeLabel(edge);
			if (label.IsEmpty())
				continue;
			edgeTexts[index] = label;
			const BPoint a = _ToView(nodes[edge.from]), b = _ToView(nodes[edge.to]);
			const float width = font.StringWidth(label.String());
			const BPoint at((a.x + b.x) / 2 - width / 2, (a.y + b.y) / 2 + height.ascent / 2 - 1);
			Candidate candidate = {false, index, direct ? 0 : (ofSelected ? 2 : 5),
				BRect(at.x - 2, at.y - height.ascent, at.x + width + 2, at.y + height.descent)};
			candidates.push_back(candidate);
		}

		std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.priority < b.priority; });
		std::vector<BRect> placed;
		for (const Candidate& candidate : candidates) {
			bool free = Bounds().Intersects(candidate.rect);
			for (size_t i = 0; free && i < placed.size(); i++)
				free = !placed[i].Intersects(candidate.rect);
			if (!free)
				continue;
			placed.push_back(candidate.rect);
			(candidate.isNode ? nodeLabelShown : edgeLabelShown)[candidate.index] = true;
		}
	}

	// the labels of the edges
	for (size_t index = 0; index < edges.size(); index++) {
		if (!edgeLabelShown[index])
			continue;
		const Edge& edge = edges[index];
		const bool strong = selecting ? emphasised(index) : static_cast<int32>(index) == fHoverEdge;
		BPoint a = _ToView(nodes[edge.from]), b = _ToView(nodes[edge.to]);
		float width = font.StringWidth(edgeTexts[index].String());
		BPoint at((a.x + b.x) / 2 - width / 2, (a.y + b.y) / 2 + height.ascent / 2 - 1);
		rgb_color halo = background;
		halo.alpha = 200;
		SetHighColor(halo);
		FillRect(BRect(at.x - 2, at.y - height.ascent, at.x + width + 2, at.y + height.descent));
		SetHighColor(strong ? textColor : Color(palette.edgeText));
		DrawString(edgeTexts[index].String(), at);
	}

	// the nodes
	for (size_t i = 0; i < nodes.size(); i++) {
		const Node& node = nodes[i];
		const bool dimmed = selecting && static_cast<int32>(i) != fSelected && !fNeighbors[i];
		const bool strong = static_cast<int32>(i) == fSelected || static_cast<int32>(i) == fHover;
		BPoint at = _ToView(node);
		float radius = _Radius(node) + (strong ? 2 : 0);

		rgb_color color = Color(sen::graph::GroupColor(node.group, fDark));
		color.alpha = dimmed ? 90 : 255;
		SetHighColor(color);
		FillEllipse(at, radius, radius);
		rgb_color ring = strong ? Color(palette.emphasis) : background;
		ring.alpha = dimmed ? 90 : 255;
		SetHighColor(ring);
		SetPenSize(strong ? 2.0f : 1.2f);
		StrokeEllipse(at, radius, radius);
	}

	// the names
	for (size_t i = 0; i < nodes.size(); i++) {
		if (!nodeLabelShown[i])
			continue;
		const Node& node = nodes[i];
		BPoint at = _ToView(node);
		float width = font.StringWidth(nodeTexts[i].String());
		BPoint where(at.x - width / 2, at.y + _Radius(node) + height.ascent + 2);
		rgb_color halo = background;
		halo.alpha = 210;
		SetHighColor(halo);
		FillRect(BRect(where.x - 2, where.y - height.ascent, where.x + width + 2, where.y + height.descent));
		SetHighColor(textColor);
		DrawString(nodeTexts[i].String(), where);
	}

	// what is under the mouse: a node, or a line
	BString lines[3];
	int lineCount = 0;
	if (fHover >= 0) {
		const Node& node = nodes[fHover];
		lines[0] = node.label.c_str();
		lines[1] = node.group.c_str();
		lines[2] << node.degree << (node.degree == 1 ? " relation" : " relations");
		lineCount = 3;
	} else if (fHoverEdge >= 0) {
		const Edge& edge = edges[fHoverEdge];
		lines[0] = nodes[edge.from].label.c_str();
		lines[0] << (edge.reciprocal ? "  <->  " : "  ->  ") << nodes[edge.to].label.c_str();
		lines[1] = EdgeLabel(edge);
		if (!edge.type.empty())
			lines[1] << (lines[1].IsEmpty() ? "" : "  ") << "(" << ShortTypeName(edge.type) << ")";
		lines[2] = "Double click: its relations";
		lineCount = 3;
	}
	if (lineCount > 0) {
		float width = 0;
		for (int line = 0; line < lineCount; line++)
			width = std::max(width, font.StringWidth(lines[line].String()));
		float lineHeight = height.ascent + height.descent + height.leading + 1;
		BRect box(fMouse.x + 14, fMouse.y + 14, fMouse.x + 14 + width + 12, fMouse.y + 14 + lineHeight * lineCount + 8);
		if (box.right > Bounds().right)
			box.OffsetBy(Bounds().right - box.right - 4, 0);
		if (box.bottom > Bounds().bottom)
			box.OffsetBy(0, Bounds().bottom - box.bottom - 4);
		SetHighColor(fDark ? Color({0x2a, 0x2d, 0x35}) : ui_color(B_TOOL_TIP_BACKGROUND_COLOR));
		FillRoundRect(box, 3, 3);
		SetHighColor(fDark ? Color({0x55, 0x5b, 0x66}) : tint_color(ui_color(B_TOOL_TIP_BACKGROUND_COLOR), B_DARKEN_3_TINT));
		StrokeRoundRect(box, 3, 3);
		SetHighColor(fDark ? textColor : ui_color(B_TOOL_TIP_TEXT_COLOR));
		for (int line = 0; line < lineCount; line++)
			DrawString(lines[line].String(), BPoint(box.left + 6, box.top + 4 + height.ascent + line * lineHeight));
	}
}


// ---- the window

struct WorkRequest {
	BMessenger							target;
	bool								collect;
	bool								withMeta;
	bool								withUnconnected;
	sen::graph::Graph					all;
	std::map<std::string, entry_ref>	refs;
	bool								truncated;
	int32								objects;
};


struct WorkResult {
	sen::graph::Graph					shown;
	sen::graph::Graph					all;
	std::map<std::string, entry_ref>	refs;
	bool								truncated;
	int32								objects;
	status_t							error;
};


GraphWindow::GraphWindow()
	:
	BWindow(BRect(100, 100, 1060, 740), "SEN Graph", B_DOCUMENT_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fSavePanel(NULL),
	fWorking(false),
	fTruncated(false),
	fObjects(0)
{
	fView = new GraphView();
	fMeta = new BCheckBox("meta", "Classifications and contexts", new BMessage(kFilter));
	fUnconnected = new BCheckBox("unconnected", "Objects without relations", new BMessage(kFilter));
	fDark = new BCheckBox("dark", "Dark", new BMessage(kDark));
	fDark->SetValue(fView->Dark() ? B_CONTROL_ON : B_CONTROL_OFF);
	fRefresh = new BButton("refresh", "Refresh", new BMessage(kRefresh));
	BButton* save = new BButton("save", "Save as SVG" B_UTF8_ELLIPSIS, new BMessage(kSave));
	fStatus = new BStringView("status", "");

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.AddGroup(B_HORIZONTAL)
			.Add(fMeta)
			.Add(fUnconnected)
			.Add(fDark)
			.AddGlue()
			.Add(save)
			.Add(fRefresh)
		.End()
		.Add(fView, 10.0f)
		.AddGroup(B_HORIZONTAL, 0)
			.Add(fStatus)
			.AddGlue()
			.AddStrut(B_V_SCROLL_BAR_WIDTH)	// (the resize handle of a document window is in the corner)
		.End()
	.End();

	fView->SetMessage("Reading the objects" B_UTF8_ELLIPSIS);
	_Start(true);
}


GraphWindow::~GraphWindow()
{
	delete fSavePanel;
}


void
GraphWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kRefresh:
			_Start(true);
			break;
		case kFilter:
			_Start(false);
			break;
		case kDark:
			fView->SetDark(fDark->Value() == B_CONTROL_ON);
			break;
		case kWorkDone:
			_Done(message);
			break;
		case kOpenNode:
			_Open(message);
			break;
		case kOpenRelations:
			_OpenRelations(message);
			break;
		case kSelectNode:
			fView->SelectId(message->GetString("id", ""));
			break;
		case kSave:
		case B_SAVE_REQUESTED:
			_Save(message);
			break;
		case kActivate:
			Activate();
			break;
		default:
			BWindow::MessageReceived(message);
	}
}


int32
GraphWindow::_WorkEntry(void* data)
{
	WorkRequest* request = (WorkRequest*)data;
	WorkResult* result = new WorkResult;
	result->error = B_OK;
	result->truncated = request->truncated;
	result->objects = request->objects;

	if (request->collect) {
		sen::graph::Collected collected;
		result->error = sen::graph::Collect(&collected, kMaxObjects);
		result->all = collected.graph;
		result->refs = collected.refs;
		result->truncated = collected.truncated;
		result->objects = collected.objects;
	} else {
		result->all = request->all;
		result->refs = request->refs;
	}

	result->shown = result->all.Filtered(request->withMeta, request->withUnconnected);
	result->shown.Layout();

	BMessage done(kWorkDone);
	done.AddPointer("result", result);
	if (request->target.SendMessage(&done) != B_OK)
		delete result;	// (the window is gone)
	delete request;
	return 0;
}


void
GraphWindow::_Start(bool collect)
{
	if (fWorking)
		return;
	fWorking = true;
	fRefresh->SetEnabled(false);
	fStatus->SetText(collect ? "Reading the objects" B_UTF8_ELLIPSIS : "Laying out" B_UTF8_ELLIPSIS);

	WorkRequest* request = new WorkRequest;
	request->target = BMessenger(this);
	request->collect = collect;
	request->withMeta = fMeta->Value() == B_CONTROL_ON;
	request->withUnconnected = fUnconnected->Value() == B_CONTROL_ON;
	request->truncated = fTruncated;
	request->objects = fObjects;
	if (!collect) {
		request->all = fAll;
		request->refs = fRefs;
	}

	thread_id thread = spawn_thread(_WorkEntry, "SEN graph", B_LOW_PRIORITY, request);
	if (thread >= 0)
		resume_thread(thread);
	else {
		delete request;
		fWorking = false;
		fRefresh->SetEnabled(true);
		fStatus->SetText("Could not start the thread");
	}
}


void
GraphWindow::_Done(BMessage* message)
{
	WorkResult* result = NULL;
	if (message->FindPointer("result", (void**)&result) != B_OK || result == NULL)
		return;
	fWorking = false;
	fRefresh->SetEnabled(true);

	if (result->error != B_OK && result->all.Nodes().empty()) {
		fView->SetGraph(sen::graph::Graph());
		fView->SetMessage("The objects could not be read");
		BString status("Could not read the objects: ");
		status << strerror(result->error);
		fStatus->SetText(status.String());
		delete result;
		return;
	}

	fAll = result->all;
	fRefs = result->refs;
	fTruncated = result->truncated;
	fObjects = result->objects;

	if (result->shown.Nodes().empty()) {
		fView->SetMessage(result->all.Nodes().empty() ? "There are no objects yet" : "No object has a relation yet (see the choices above)");
	}
	fView->SetGraph(result->shown);

	BString status;
	status << static_cast<int32>(result->shown.Nodes().size()) << " of " << static_cast<int32>(fAll.Nodes().size()) << " objects, "
		<< static_cast<int32>(result->shown.Edges().size()) << " relations";
	if (fTruncated)
		status << " (the first " << static_cast<int32>(kMaxObjects) << " of " << fObjects << " objects were read)";
	status << ".  Click: select, double click: open (a line: its relations), wheel: zoom, F: fit.";
	fStatus->SetText(status.String());
	delete result;
}


void
GraphWindow::_Open(const BMessage* message)
{
	auto found = fRefs.find(message->GetString("id", ""));
	if (found == fRefs.end())
		return;
	// as a double click in Tracker does (what Akita shows for an ontology or an entity is what it does then)
	BMessage open(B_REFS_RECEIVED);
	open.AddRef("refs", &found->second);
	BMessenger(kTrackerSignature).SendMessage(&open);
}


void
GraphWindow::_OpenRelations(const BMessage* message)
{
	auto found = fRefs.find(message->GetString("id", ""));
	if (found == fRefs.end())
		return;
	// Akita shows the folder of the relations of that type of the file (as the menu Open related... does), or all of them if the type is not known
	const char* type = message->GetString("type", "");
	BMessage open(type[0] != '\0' ? sen::cmd::kOpenRelationsOfType : sen::cmd::kOpenAllRelationsView);
	open.AddRef(sen::key::kSourceRef, &found->second);
	if (type[0] != '\0')
		open.AddString(sen::key::kRelationType, type);
	BMessenger(kTrackerSignature).SendMessage(&open);
}


void
GraphWindow::_Save(const BMessage* message)
{
	if (message->what == kSave) {
		if (fSavePanel == NULL) {
			BMessenger target(this);
			fSavePanel = new BFilePanel(B_SAVE_PANEL, &target, NULL, B_FILE_NODE, false);
		}
		fSavePanel->SetSaveText("sen-graph.svg");
		fSavePanel->Show();
		return;
	}

	entry_ref directory;
	const char* name;
	if (message->FindRef("directory", &directory) != B_OK || message->FindString("name", &name) != B_OK)
		return;

	sen::graph::SvgOptions options;
	options.dark = fView->Dark();	// as it is seen
	const std::string svg = fView->Graph().ToSvg(options);
	BDirectory folder(&directory);
	BFile file(&folder, name, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status_t result = file.InitCheck();
	if (result == B_OK) {
		ssize_t written = file.Write(svg.data(), svg.size());
		result = written == static_cast<ssize_t>(svg.size()) ? B_OK : (written < 0 ? written : B_ERROR);
	}
	if (result == B_OK) {
		BNodeInfo(&file).SetType("image/svg+xml");
		BString status("Saved as ");
		status << name;
		fStatus->SetText(status.String());
	} else {
		BString text("The graph could not be saved: ");
		text << strerror(result);
		BAlert* alert = new BAlert("SEN Graph", text.String(), "OK", NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go(NULL);
	}
}

}	// namespace ui
}	// namespace sen
