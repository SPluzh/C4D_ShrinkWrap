#include "shrinkwrap.h"
#include "c4d_basecontainer.h"
#include "c4d_baseobject.h"
#include "c4d_basedocument.h"
#include "c4d_basetag.h"
#include "c4d_general.h"
#include "c4d_resource.h"
#include "c4d_accessedobjects.h"
#include "c4d_basedraw.h"
#include "description/dbasedraw.h"
#include "description/obase.h"
#include "maxon/parallelfor.h"
#include <chrono>

using namespace cinema;
using namespace maxon;

static void HashMatrix(const Matrix& m, UInt64& hash)
{
	const Float* f = (const Float*)&m;
	for (Int32 i = 0; i < 12; ++i)
	{
		UInt64 bits = *(const UInt64*)&f[i];
		hash ^= (bits + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2));
	}
}

static void RecurseCacheDirty(const BaseObject* cache, UInt64& dirtySum, Int32& objCount, Int32& polyCount, Int32& pointCount, Int32 depth)
{
	for (const BaseObject* c = cache; c && depth <= 32; c = c->GetNext())
	{
		objCount++;
		dirtySum ^= (c->GetDirty(DIRTYFLAGS::DATA | DIRTYFLAGS::MATRIX) + 0x9e3779b97f4a7c15ULL + (dirtySum << 6) + (dirtySum >> 2));
		HashMatrix(c->GetMl(), dirtySum);

		if (c->IsInstanceOf(Opolygon))
		{
			const PolygonObject* p = static_cast<const PolygonObject*>(c);
			Int32 pc = p->GetPointCount();
			polyCount += p->GetPolygonCount();
			pointCount += pc;
			if (pc > 0)
			{
				const Vector* pts = p->GetPointR();
				if (pts)
				{
					const Float* f0 = (const Float*)&pts[0];
					const Float* f1 = (const Float*)&pts[pc - 1];
					for (Int32 i = 0; i < 3; ++i)
					{
						dirtySum ^= (*(const UInt64*)&f0[i] + 0x9e3779b97f4a7c15ULL + (dirtySum << 6) + (dirtySum >> 2));
						dirtySum ^= (*(const UInt64*)&f1[i] + 0x9e3779b97f4a7c15ULL + (dirtySum << 6) + (dirtySum >> 2));
					}
				}
			}
		}

		if (c->GetDeformCache())
			RecurseCacheDirty(c->GetDeformCache(), dirtySum, objCount, polyCount, pointCount, depth + 1);
		else if (c->GetCache())
			RecurseCacheDirty(c->GetCache(), dirtySum, objCount, polyCount, pointCount, depth + 1);

		if (c->GetDown())
			RecurseCacheDirty(c->GetDown(), dirtySum, objCount, polyCount, pointCount, depth + 1);
	}
}

static void RecurseSceneDirty(const BaseObject* op, UInt64& dirtySum, Int32& objCount, Int32& polyCount, Int32& pointCount, Int32 depth = 0)
{
	if (!op || depth > 32)
		return;

	objCount++;
	dirtySum ^= (op->GetDirty(DIRTYFLAGS::DATA | DIRTYFLAGS::MATRIX) + 0x9e3779b97f4a7c15ULL + (dirtySum << 6) + (dirtySum >> 2));
	HashMatrix(op->GetMg(), dirtySum);

	if (op->IsInstanceOf(Opolygon))
	{
		const PolygonObject* p = static_cast<const PolygonObject*>(op);
		Int32 pc = p->GetPointCount();
		polyCount += p->GetPolygonCount();
		pointCount += pc;
		if (pc > 0)
		{
			const Vector* pts = p->GetPointR();
			if (pts)
			{
				const Float* f0 = (const Float*)&pts[0];
				const Float* f1 = (const Float*)&pts[pc - 1];
				for (Int32 i = 0; i < 3; ++i)
				{
					dirtySum ^= (*(const UInt64*)&f0[i] + 0x9e3779b97f4a7c15ULL + (dirtySum << 6) + (dirtySum >> 2));
					dirtySum ^= (*(const UInt64*)&f1[i] + 0x9e3779b97f4a7c15ULL + (dirtySum << 6) + (dirtySum >> 2));
				}
			}
		}
	}

	if (op->GetDeformCache())
		RecurseCacheDirty(op->GetDeformCache(), dirtySum, objCount, polyCount, pointCount, depth + 1);
	else if (op->GetCache())
		RecurseCacheDirty(op->GetCache(), dirtySum, objCount, polyCount, pointCount, depth + 1);

	for (const BaseObject* child = op->GetDown(); child; child = child->GetNext())
	{
		RecurseSceneDirty(child, dirtySum, objCount, polyCount, pointCount, depth + 1);
	}
}

static void CollectCachePolygons(const BaseObject* cache,
								 const Matrix& parentMg,
								 const BaseObject* excludeOp,
								 maxon::BaseArray<const PolygonObject*>& outPolys,
								 maxon::BaseArray<Matrix>& outMatrices,
								 Int32 depth)
{
	for (const BaseObject* c = cache; c && depth <= 32; c = c->GetNext())
	{
		if (c == excludeOp)
			continue;

		Matrix currentMg = parentMg * c->GetMl();

		if (c->GetDeformCache())
		{
			CollectCachePolygons(c->GetDeformCache(), currentMg, excludeOp, outPolys, outMatrices, depth + 1);
		}
		else if (c->GetCache())
		{
			CollectCachePolygons(c->GetCache(), currentMg, excludeOp, outPolys, outMatrices, depth + 1);
		}
		else if (c->IsInstanceOf(Opolygon))
		{
			const PolygonObject* poly = static_cast<const PolygonObject*>(c);
			if (poly->GetPolygonCount() > 0 && poly->GetPointCount() > 0)
			{
				outPolys.Append(poly) iferr_ignore("append poly");
				outMatrices.Append(currentMg) iferr_ignore("append matrix");
			}
		}

		if (c->GetDown())
		{
			CollectCachePolygons(c->GetDown(), currentMg, excludeOp, outPolys, outMatrices, depth + 1);
		}
	}
}

static void CollectScenePolygons(const BaseObject* op,
								 const BaseObject* excludeOp,
								 maxon::BaseArray<const PolygonObject*>& outPolys,
								 maxon::BaseArray<Matrix>& outMatrices,
								 Int32 depth = 0)
{
	if (!op || op == excludeOp || depth > 32)
		return;

	Matrix currentMg = op->GetMg();

	if (op->GetDeformCache())
	{
		CollectCachePolygons(op->GetDeformCache(), currentMg, excludeOp, outPolys, outMatrices, depth + 1);
		return;
	}
	else if (op->GetCache())
	{
		CollectCachePolygons(op->GetCache(), currentMg, excludeOp, outPolys, outMatrices, depth + 1);
		return;
	}
	else if ((depth == 0 || !op->GetBit(BIT_CONTROLOBJECT)) && op->IsInstanceOf(Opolygon))
	{
		const PolygonObject* poly = static_cast<const PolygonObject*>(op);
		if (poly->GetPolygonCount() > 0 && poly->GetPointCount() > 0)
		{
			outPolys.Append(poly) iferr_ignore("append poly");
			outMatrices.Append(currentMg) iferr_ignore("append matrix");
		}
	}

	for (const BaseObject* child = op->GetDown(); child; child = child->GetNext())
	{
		CollectScenePolygons(child, excludeOp, outPolys, outMatrices, depth + 1);
	}
}

static const BaseObject* ResolveGeneratorTarget(const BaseObject* targetObj)
{
	if (!targetObj)
		return nullptr;

	const BaseObject* highestGenerator = nullptr;

	for (const BaseObject* parent = targetObj->GetUp(); parent; parent = parent->GetUp())
	{
		// Stop if parent generator is disabled with green tick or editor mode OFF
		if (!parent->GetDeformMode() || parent->GetEditorMode() == MODE_OFF)
			break;

		// Check if parent is a Subdivision Surface or another caching generator (Boole, Connect, Symmetry, etc.)
		if (parent->IsInstanceOf(Osds) || parent->GetCache() || parent->GetDeformCache())
		{
			highestGenerator = parent;
		}
		else if (parent->IsInstanceOf(Onull))
		{
			// Climb through Nulls to see if there is an SDS/generator above the Null
			continue;
		}
		else
		{
			break;
		}
	}

	return highestGenerator ? highestGenerator : targetObj;
}

static void EnsureDeformedEditing(BaseDocument* doc)
{
	if (!doc)
		return;

	BaseDraw* bd = doc->GetActiveBaseDraw();
	if (bd)
	{
		BaseContainer& bc = bd->GetDataInstanceRef();
		bc.SetBool(BASEDRAW_DATA_DEFORMEDEDIT, true);
		bd->SetParameter(DescID::Create((Int32)BASEDRAW_DATA_DEFORMEDEDIT), GeData(true), DESCFLAGS_SET::NONE);
		bd->Message(MSG_CHANGE);
	}

	Int32 i = 0;
	while ((bd = doc->GetBaseDraw(i++)) != nullptr)
	{
		BaseContainer& bc = bd->GetDataInstanceRef();
		bc.SetBool(BASEDRAW_DATA_DEFORMEDEDIT, true);
		bd->SetParameter(DescID::Create((Int32)BASEDRAW_DATA_DEFORMEDEDIT), GeData(true), DESCFLAGS_SET::NONE);
	}
}

ShrinkWrapDeformer::~ShrinkWrapDeformer()
{
}

void ShrinkWrapDeformer::Free(GeListNode* node)
{
	if (node && _meshDisplayApplied && _lastParentLink && GeIsMainThreadAndNoDrawThread())
	{
		BaseObject* op = static_cast<BaseObject*>(node);
		BaseDocument* doc = op ? op->GetDocument() : nullptr;
		if (doc)
		{
			BaseObject* lastParent = (BaseObject*)_lastParentLink->GetLink(doc, Opolygon);
			if (lastParent)
			{
				ObjectColorProperties origProp;
				origProp.color = _origParentColor;
				origProp.usecolor = _origParentUseColor;
				origProp.xray = _origParentXray;
				lastParent->SetColorProperties(&origProp);
				lastParent->Message(MSG_UPDATE);
			}
		}
	}
	_meshDisplayApplied = false;
	if (_lastParentLink)
		_lastParentLink->SetLink(nullptr);

	NodeData::Free(node);
}

Bool ShrinkWrapDeformer::CopyTo(NodeData* dest, const GeListNode* snode, GeListNode* dnode, COPYFLAGS flags, AliasTrans* trn) const
{
	ShrinkWrapDeformer* dst = static_cast<ShrinkWrapDeformer*>(dest);
	if (dst)
	{
		dst->_bvh.Clear();
		dst->_checkDirtyHash = 0;
		dst->_checkDirtyTargetRoot = nullptr;
		dst->_cachedHierarchyDirty = 0;
		dst->_cachedTargetRoot = nullptr;
		dst->_cachedObjectCount = 0;
		dst->_cachedTotalPolyCount = 0;
		dst->_cachedTotalPointCount = 0;
		dst->_meshDisplayApplied = false;
		if (dst->_lastParentLink)
			dst->_lastParentLink->SetLink(nullptr);
	}
	return NodeData::CopyTo(dest, snode, dnode, flags, trn);
}

void ShrinkWrapDeformer::SyncMeshDisplay(BaseObject* deformer, Bool forceRestore) const
{
	if (!deformer)
		return;

	// Critical: Never modify scene objects outside the main UI thread or during viewport drawing
	if (!GeIsMainThreadAndNoDrawThread())
		return;

	BaseDocument* doc = deformer->GetDocument();
	if (!doc)
		return;

	BaseObject* currentParent = deformer->GetUp();
	BaseObject* lastParent = (doc && _lastParentLink) ? (BaseObject*)_lastParentLink->GetLink(doc, Opolygon) : nullptr;

	// If parent changed and we had previously applied styling to old parent, restore old parent
	if (lastParent && lastParent != currentParent && _meshDisplayApplied)
	{
		ObjectColorProperties origProp;
		origProp.color = _origParentColor;
		origProp.usecolor = _origParentUseColor;
		origProp.xray = _origParentXray;
		lastParent->SetColorProperties(&origProp);
		lastParent->Message(MSG_UPDATE);

		_meshDisplayApplied = false;
		if (_lastParentLink)
			_lastParentLink->SetLink(nullptr);
	}

	if (forceRestore || !currentParent || !currentParent->IsInstanceOf(Opolygon))
	{
		if (_meshDisplayApplied && lastParent)
		{
			ObjectColorProperties origProp;
			origProp.color = _origParentColor;
			origProp.usecolor = _origParentUseColor;
			origProp.xray = _origParentXray;
			lastParent->SetColorProperties(&origProp);
			lastParent->Message(MSG_UPDATE);

			_meshDisplayApplied = false;
			if (_lastParentLink)
				_lastParentLink->SetLink(nullptr);
		}
		return;
	}

	const BaseContainer& data = deformer->GetDataInstanceRef();
	Bool isDeformerOn = deformer->GetDeformMode() && (deformer->GetEditorMode() != MODE_OFF);
	Bool customColor = data.GetBool(SHRINKWRAP_ENABLE_CUSTOM_COLOR, true);

	if (!isDeformerOn || !customColor)
	{
		// Deformer is inactive or custom color disabled: restore parent's original appearance
		if (_meshDisplayApplied && lastParent)
		{
			ObjectColorProperties origProp;
			origProp.color = _origParentColor;
			origProp.usecolor = _origParentUseColor;
			origProp.xray = _origParentXray;
			lastParent->SetColorProperties(&origProp);
			lastParent->Message(MSG_UPDATE);
			_meshDisplayApplied = false;
			if (_lastParentLink)
				_lastParentLink->SetLink(nullptr);
		}
		return;
	}

	// Deformer is active and custom color is enabled
	Vector meshColor = data.GetVector(SHRINKWRAP_MESH_COLOR, Vector(0.0, 150.0 / 255.0, 1.0));
	Float faceOpacity = data.GetFloat(SHRINKWRAP_FACE_OPACITY, 0.35);
	if (faceOpacity > 1.0) faceOpacity /= 100.0;
	if (faceOpacity < 0.0) faceOpacity = 0.0;
	Bool useXray = (faceOpacity < 0.99);

	if (!_meshDisplayApplied || lastParent != currentParent)
	{
		// First time applying to this parent: capture original properties
		ObjectColorProperties curProp;
		currentParent->GetColorProperties(&curProp);
		_origParentColor = curProp.color;
		_origParentUseColor = curProp.usecolor;
		_origParentXray = curProp.xray;
		if (_lastParentLink)
			_lastParentLink->SetLink(currentParent);
		_meshDisplayApplied = true;
	}

	// Set desired retopo display styling
	ObjectColorProperties targetProp;
	targetProp.color = meshColor;
	targetProp.usecolor = ID_BASEOBJECT_USECOLOR_ALWAYS;
	targetProp.xray = useXray;

	ObjectColorProperties checkProp;
	currentParent->GetColorProperties(&checkProp);
	if (checkProp.color != targetProp.color || checkProp.usecolor != targetProp.usecolor || checkProp.xray != targetProp.xray)
	{
		currentParent->SetColorProperties(&targetProp);
		currentParent->Message(MSG_UPDATE);
	}
}

Bool ShrinkWrapDeformer::Init(GeListNode* node, Bool isCloneInit)
{
	if (!node)
		return false;

	if (isCloneInit)
	{
		_meshDisplayApplied = false;
		if (_lastParentLink)
			_lastParentLink->SetLink(nullptr);
		_checkDirtyHash = 0;
		_checkDirtyTargetRoot = nullptr;
		_cachedHierarchyDirty = 0;
		_cachedTargetRoot = nullptr;
		_cachedObjectCount = 0;
		_cachedTotalPolyCount = 0;
		_cachedTotalPointCount = 0;
		_bvh.Clear();
		return true;
	}

	BaseContainer& data = static_cast<BaseObject*>(node)->GetDataInstanceRef();
	data.SetInt32(SHRINKWRAP_MODE, SHRINKWRAP_MODE_NEAREST_SURFACE);
	data.SetBool(SHRINKWRAP_USE_SUBDIV, true);
	data.SetFloat(SHRINKWRAP_OFFSET, 0.0);
	data.SetFloat(SHRINKWRAP_STRENGTH, 1.0);
	data.SetFloat(SHRINKWRAP_FALLOFF_RADIUS, 0.0);
	data.SetBool(SHRINKWRAP_BIDIRECTIONAL, true);
	data.SetBool(SHRINKWRAP_ABOVE_SURFACE, false);
	data.SetBool(SHRINKWRAP_AUTO_BAKE, true);
	data.SetBool(SHRINKWRAP_SNAP_VERTICES_EDGES, false);

	// QuadDraw Retopo Display styling defaults
	data.SetBool(SHRINKWRAP_ENABLE_CUSTOM_COLOR, true);
	data.SetVector(SHRINKWRAP_MESH_COLOR, Vector(0.0, 150.0 / 255.0, 1.0)); // QuadDraw Cyan (0, 150, 255)
	data.SetFloat(SHRINKWRAP_FACE_OPACITY, 0.35); // 35% opacity (QuadDraw style)
	data.SetBool(SHRINKWRAP_DRAW_WIREFRAME, true);
	data.SetVector(SHRINKWRAP_WIRE_COLOR, Vector(0.0, 0.0, 0.0)); // Crisp black wireframe
	data.SetFloat(SHRINKWRAP_WIRE_WIDTH, 1.5);

	return true;
}

Bool ShrinkWrapDeformer::Message(GeListNode* node, Int32 type, void* data)
{
	if (!node)
		return true;

	BaseObject* op = static_cast<BaseObject*>(node);
	BaseDocument* doc = op->GetDocument();

	if (type == MSG_MENUPREPARE)
	{
		op->SetDeformMode(true);
		if (doc)
		{
			EnsureDeformedEditing(doc);
		}
		SyncMeshDisplay(op);
		EventAdd();
	}
	else if (type == MSG_DESCRIPTION_CHECKUPDATE)
	{
		_cachedHierarchyDirty = 0;
		if (doc)
		{
			EnsureDeformedEditing(doc);
		}
		SyncMeshDisplay(op);
		BaseObject* parent = op->GetUp();
		if (parent)
		{
			parent->SetDirty(DIRTYFLAGS::DATA);
		}
		EventAdd();
	}
	else if (type == MSG_DESCRIPTION_POSTSETPARAMETER)
	{
		_cachedHierarchyDirty = 0;
		SyncMeshDisplay(op);
		BaseObject* parent = op->GetUp();
		if (parent)
		{
			parent->SetDirty(DIRTYFLAGS::DATA);
		}
		EventAdd();
	}
	else if (type == MSG_CHANGE)
	{
		if (doc && GeIsMainThreadAndNoDrawThread())
		{
			SyncMeshDisplay(op);
		}
	}
	else if (type == MSG_DOCUMENTINFO)
	{
		DocumentInfoData* docInfo = static_cast<DocumentInfoData*>(data);
		if (docInfo)
		{
			if (docInfo->type == MSG_DOCUMENTINFO_TYPE_SAVE_BEFORE)
			{
				SyncMeshDisplay(op, true); // Restore original properties before saving scene to file
			}
			else if (docInfo->type == MSG_DOCUMENTINFO_TYPE_SAVE_AFTER)
			{
				SyncMeshDisplay(op, false); // Re-apply retopo display styling after save
			}
		}
	}
	else if (type == MSG_DESCRIPTION_COMMAND)
	{
		DescriptionCommand* dc = static_cast<DescriptionCommand*>(data);
		if (dc && dc->_descId.GetDepth() > 0)
		{
			Int32 cmdId = (Int32)dc->_descId[0].id;
			if (cmdId == SHRINKWRAP_ENABLE_DEFORMED_EDITING)
			{
				if (doc)
				{
					EnsureDeformedEditing(doc);
					EventAdd();
				}
			}
			else if (cmdId == SHRINKWRAP_APPLY_TO_MESH)
			{
				if (doc)
				{
					BaseObject* parent = op->GetUp();
					if (parent && parent->IsInstanceOf(Opolygon))
					{
						PolygonObject* poly = static_cast<PolygonObject*>(parent);
						const PolygonObject* defCache = poly->GetDeformCache() ? static_cast<const PolygonObject*>(poly->GetDeformCache()) : nullptr;
						if (defCache && defCache->GetPointCount() == poly->GetPointCount())
						{
							doc->StartUndo();
							doc->AddUndo(UNDOTYPE::CHANGE, poly);
							CopyMem(defCache->GetPointR(), poly->GetPointW(), sizeof(Vector) * poly->GetPointCount());
							poly->Message(MSG_UPDATE);
							doc->EndUndo();
							EventAdd();
						}
					}
				}
			}
		}
	}
	return true;
}

void ShrinkWrapDeformer::CheckDirty(BaseObject* op, const BaseDocument* doc)
{
	if (!op || !doc)
		return;

	const BaseContainer& data = op->GetDataInstanceRef();
	const BaseObject* targetObj = data.GetObjectLink(SHRINKWRAP_TARGET_LINK, doc);
	if (targetObj)
	{
		Bool useSubdiv = data.GetBool(SHRINKWRAP_USE_SUBDIV, true);
		const BaseObject* resolvedTarget = useSubdiv ? ResolveGeneratorTarget(targetObj) : targetObj;
		UInt64 hierDirty = 0;
		Int32 objCount = 0;
		Int32 polyCount = 0;
		Int32 pointCount = 0;
		RecurseSceneDirty(resolvedTarget, hierDirty, objCount, polyCount, pointCount);
		if (targetObj != resolvedTarget)
		{
			RecurseSceneDirty(targetObj, hierDirty, objCount, polyCount, pointCount);
		}

		if (hierDirty != _checkDirtyHash || resolvedTarget != _checkDirtyTargetRoot)
		{
			_checkDirtyHash = hierDirty;
			_checkDirtyTargetRoot = resolvedTarget;
			op->SetDirty(DIRTYFLAGS::DATA);
			BaseObject* parent = op->GetUp();
			if (parent)
			{
				parent->SetDirty(DIRTYFLAGS::DATA);
			}
		}
	}
	else if (_checkDirtyTargetRoot != nullptr)
	{
		_checkDirtyHash = 0;
		_checkDirtyTargetRoot = nullptr;
		op->SetDirty(DIRTYFLAGS::DATA);
		BaseObject* parent = op->GetUp();
		if (parent)
		{
			parent->SetDirty(DIRTYFLAGS::DATA);
		}
	}
}

DRAWRESULT ShrinkWrapDeformer::Draw(BaseObject* op, DRAWPASS drawpass, BaseDraw* bd, BaseDrawHelp* bh)
{
	if (!op || !bd || !bh)
		return DRAWRESULT::OK;

	if (drawpass != DRAWPASS::OBJECT)
		return DRAWRESULT::OK;

	// Only draw wireframe overlay when deformer is enabled and visible
	if (!op->GetDeformMode() || op->GetEditorMode() == MODE_OFF)
		return DRAWRESULT::OK;

	const BaseContainer& data = op->GetDataInstanceRef();
	Bool drawWireframe = data.GetBool(SHRINKWRAP_DRAW_WIREFRAME, true);
	if (!drawWireframe)
		return DRAWRESULT::OK;

	BaseObject* parent = op->GetUp();
	if (!parent || parent->GetEditorMode() == MODE_OFF)
		return DRAWRESULT::OK;

	const PolygonObject* poly = nullptr;
	if (parent->GetDeformCache() && parent->GetDeformCache()->IsInstanceOf(Opolygon))
		poly = static_cast<const PolygonObject*>(parent->GetDeformCache());
	else if (parent->IsInstanceOf(Opolygon))
		poly = static_cast<const PolygonObject*>(parent);

	if (!poly || poly->GetPolygonCount() <= 0 || poly->GetPointCount() <= 0)
		return DRAWRESULT::OK;

	Int32 polyCount = poly->GetPolygonCount();
	const CPolygon* polys = poly->GetPolygonR();
	const Vector* pts = poly->GetPointR();
	if (!polys || !pts)
		return DRAWRESULT::OK;

	Matrix rMg = parent->GetMg();
	// zoffset = 2 ensures wireframe lines are strictly in front of polygons with zero Z-fighting
	bd->SetMatrix_Matrix(nullptr, Matrix(), 2);

	auto drawThickLine = [&](const Vector& p1, const Vector& p2, Float width)
	{
		bd->DrawLine(p1, p2, 0);
		if (width >= 1.35)
		{
			Vector s1 = bd->WS(p1);
			Vector s2 = bd->WS(p2);
			Vector dir = Vector(s2.x - s1.x, s2.y - s1.y, 0.0);
			Float len = maxon::Sqrt(dir.x * dir.x + dir.y * dir.y);
			if (len > 0.001)
			{
				Vector perp(-dir.y / len, dir.x / len, 0.0);
				Int32 extraSteps = (Int32)maxon::Floor((width - 0.7) * 0.8) + 1;
				if (extraSteps < 1) extraSteps = 1;
				if (extraSteps > 4) extraSteps = 4;

				for (Int32 step = 1; step <= extraSteps; ++step)
				{
					Float offset = Float(step);
					Vector p1_a = bd->SW(Vector(s1.x + perp.x * offset, s1.y + perp.y * offset, s1.z));
					Vector p2_a = bd->SW(Vector(s2.x + perp.x * offset, s2.y + perp.y * offset, s2.z));
					Vector p1_b = bd->SW(Vector(s1.x - perp.x * offset, s1.y - perp.y * offset, s1.z));
					Vector p2_b = bd->SW(Vector(s2.x - perp.x * offset, s2.y - perp.y * offset, s2.z));
					bd->DrawLine(p1_a, p2_a, 0);
					bd->DrawLine(p1_b, p2_b, 0);
				}
			}
		}
	};

	Vector wireColor = data.GetVector(SHRINKWRAP_WIRE_COLOR, Vector(0.0, 0.0, 0.0));
	Float lineWidth = data.GetFloat(SHRINKWRAP_WIRE_WIDTH, 1.5);
	if (lineWidth < 1.0) lineWidth = 1.0;
	if (lineWidth > 10.0) lineWidth = 10.0;

	GeData oldLineWidth = bd->GetDrawParam(DRAW_PARAMETER_LINEWIDTH);
	bd->SetDrawParam(DRAW_PARAMETER_LINEWIDTH, GeData(lineWidth));
	bd->SetTransparency(0);
	bd->SetPen(wireColor);

	for (Int32 i = 0; i < polyCount; ++i)
	{
		const CPolygon& p = polys[i];
		Bool isQuad = (p.c != p.d);

		Vector qPts[4] = {
			rMg * pts[p.a],
			rMg * pts[p.b],
			rMg * pts[p.c],
			rMg * pts[p.d]
		};

		drawThickLine(qPts[0], qPts[1], lineWidth);
		drawThickLine(qPts[1], qPts[2], lineWidth);
		if (isQuad)
		{
			drawThickLine(qPts[2], qPts[3], lineWidth);
			drawThickLine(qPts[3], qPts[0], lineWidth);
		}
		else
		{
			drawThickLine(qPts[2], qPts[0], lineWidth);
		}
	}

	bd->SetDrawParam(DRAW_PARAMETER_LINEWIDTH, oldLineWidth);
	bd->SetTransparency(0);
	return DRAWRESULT::OK;
}

void ShrinkWrapDeformer::GetDimension(const BaseObject* op, Vector* mp, Vector* rad) const
{
	if (mp)
		*mp = Vector(0.0);
	if (rad)
		*rad = Vector(20.0);
}

Result<Bool> ShrinkWrapDeformer::GetAccessedObjects(const BaseList2D* node, METHOD_ID method, AccessedObjectsCallback& access) const
{
	return access.MayAccessAnything();
}

Bool ShrinkWrapDeformer::ModifyObject(const BaseObject* mod, const BaseDocument* doc,
									  BaseObject* op, const Matrix& op_mg,
									  const Matrix& mod_mg, Float lod,
									  Int32 flags, BaseThread* thread) const
{
	if (!mod || !op || !doc)
		return true;

	if (!op->IsInstanceOf(Opoint))
		return true;

	const BaseContainer& data = mod->GetDataInstanceRef();
	const BaseObject* targetObj = data.GetObjectLink(SHRINKWRAP_TARGET_LINK, doc);
	if (!targetObj || targetObj == op || targetObj == mod)
		return true;

	const BaseObject* check = targetObj;
	while (check)
	{
		if (check == op || check == mod)
			return true;
		check = check->GetUp();
	}
	check = op;
	while (check)
	{
		if (check == targetObj)
			return true;
		check = check->GetUp();
	}

	Bool useSubdiv = data.GetBool(SHRINKWRAP_USE_SUBDIV, true);
	const BaseObject* resolvedTarget = useSubdiv ? ResolveGeneratorTarget(targetObj) : targetObj;
	if (!resolvedTarget || resolvedTarget == op || resolvedTarget == mod)
		return true;

	if (resolvedTarget != targetObj)
	{
		check = resolvedTarget;
		while (check)
		{
			if (check == op || check == mod)
				return true;
			check = check->GetUp();
		}
		check = op;
		while (check)
		{
			if (check == resolvedTarget)
				return true;
			check = check->GetUp();
		}
	}

	Float strength = data.GetFloat(SHRINKWRAP_STRENGTH);
	if (strength <= 0.0)
		return true;

	PointObject* ptOp = ToPoint(op);
	Vector* padr = ptOp->GetPointW();
	Int32 pcnt = ptOp->GetPointCount();
	if (!padr || pcnt <= 0)
		return true;

	maxon::BaseArray<const PolygonObject*> targetPolys;
	maxon::BaseArray<Matrix> targetMatrices;
	CollectScenePolygons(resolvedTarget, op, targetPolys, targetMatrices);

	if (targetPolys.GetCount() == 0 && resolvedTarget != targetObj)
	{
		resolvedTarget = targetObj;
		CollectScenePolygons(resolvedTarget, op, targetPolys, targetMatrices);
	}

	if (targetPolys.GetCount() == 0)
		return true;

	UInt64 hierDirty = 0;
	Int32 hierObjCount = 0;
	Int32 hierPolyCount = 0;
	Int32 hierPointCount = 0;
	RecurseSceneDirty(resolvedTarget, hierDirty, hierObjCount, hierPolyCount, hierPointCount);
	if (targetObj != resolvedTarget)
	{
		RecurseSceneDirty(targetObj, hierDirty, hierObjCount, hierPolyCount, hierPointCount);
	}
	for (Int32 k = 0; k < (Int32)targetMatrices.GetCount(); ++k)
	{
		HashMatrix(targetMatrices[k], hierDirty);
	}

	Int32 totalPolys = 0;
	Int32 totalPoints = 0;
	for (Int32 k = 0; k < (Int32)targetPolys.GetCount(); ++k)
	{
		const PolygonObject* p = targetPolys[k];
		if (p)
		{
			totalPolys += p->GetPolygonCount();
			totalPoints += p->GetPointCount();
		}
	}

	// BVH Caching with dirty check
	if (!_bvh.IsBuilt() || hierDirty != _cachedHierarchyDirty || resolvedTarget != _cachedTargetRoot ||
		(Int32)targetPolys.GetCount() != _cachedObjectCount || totalPolys != _cachedTotalPolyCount ||
		totalPoints != _cachedTotalPointCount)
	{
		auto t0 = std::chrono::high_resolution_clock::now();
		Matrix invTargetMg = ~resolvedTarget->GetMg();
		if (!_bvh.Build(targetPolys, targetMatrices, invTargetMg, thread))
			return true;
		auto t1 = std::chrono::high_resolution_clock::now();
		double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
		ApplicationOutput("ShrinkWrap: BVH built for @ objects, @ polygons (@ triangles) in @ ms."_s,
						  (Int32)targetPolys.GetCount(), totalPolys, _bvh.GetTriangleCount(), ms);

		_cachedHierarchyDirty = hierDirty;
		_cachedTargetRoot = resolvedTarget;
		_cachedObjectCount = (Int32)targetPolys.GetCount();
		_cachedTotalPolyCount = totalPolys;
		_cachedTotalPointCount = totalPoints;
	}

	if (!_bvh.IsBuilt())
		return true;

	Int32 mode = data.GetInt32(SHRINKWRAP_MODE);
	Float offset = data.GetFloat(SHRINKWRAP_OFFSET);
	Float falloffRadius = data.GetFloat(SHRINKWRAP_FALLOFF_RADIUS);
	Bool bidirectional = data.GetBool(SHRINKWRAP_BIDIRECTIONAL);
	Bool aboveSurface = data.GetBool(SHRINKWRAP_ABOVE_SURFACE);
	Bool snapToVertsEdges = data.GetBool(SHRINKWRAP_SNAP_VERTICES_EDGES, false);

	Matrix targetMg = resolvedTarget->GetMg();
	Matrix invTargetMg = ~targetMg;
	Matrix invOpMg = ~op_mg;

	// Extract active viewport camera for uniform surface projection
	BaseDraw* bd = doc ? doc->GetActiveBaseDraw() : nullptr;
	Vector camPos(0.0);
	Vector camForward(0.0, 0.0, 1.0);
	Bool isPerspective = true;
	Bool hasCamera = false;

	if (bd)
	{
		Matrix camMg = bd->GetMg();
		camPos = camMg.off;
		camForward = camMg.sqmat.v3;
		if (camForward.GetSquaredLength() > 1e-12)
			camForward.Normalize();
		else
			camForward = Vector(0.0, 0.0, 1.0);

		// In Cinema 4D, Pperspective == 0. All other projections are parallel/orthographic.
		isPerspective = (bd->GetProjection() == 0);
		hasCamera = true;
	}

	// Vertex Map handling
	const BaseList2D* vmapLink = data.GetObjectLink(SHRINKWRAP_VERTEXMAP_LINK, doc);
	const Float32* vmapWeights = nullptr;
	if (vmapLink && vmapLink->IsInstanceOf(Tvertexmap))
	{
		const VertexMapTag* vtag = static_cast<const VertexMapTag*>(vmapLink);
		vmapWeights = vtag->GetDataAddressR();
	}

	const Float32* autoWeights = nullptr;
	if (!vmapWeights)
	{
		autoWeights = ptOp->CalcVertexMap(mod);
	}

	// Compute vertex normals for Project mode
	maxon::BaseArray<Vector> vertexNormals;
	if (mode == SHRINKWRAP_MODE_PROJECT && op->IsInstanceOf(Opolygon))
	{
		PolygonObject* polyOp = static_cast<PolygonObject*>(op);
		const CPolygon* polys = polyOp->GetPolygonR();
		Int32 polyCount = polyOp->GetPolygonCount();

		vertexNormals.Resize(pcnt) iferr_ignore("vnormals alloc");
		for (Int32 i = 0; i < pcnt; ++i)
			vertexNormals[i] = Vector(0.0);

		for (Int32 i = 0; i < polyCount; ++i)
		{
			const CPolygon& p = polys[i];
			Vector fn = Cross(padr[p.b] - padr[p.a], padr[p.c] - padr[p.a]);
			vertexNormals[p.a] += fn;
			vertexNormals[p.b] += fn;
			vertexNormals[p.c] += fn;
			if (p.c != p.d)
			{
				Vector fn2 = Cross(padr[p.c] - padr[p.a], padr[p.d] - padr[p.a]);
				vertexNormals[p.a] += fn2;
				vertexNormals[p.b] += fn2;
				vertexNormals[p.d] += fn2;
			}
		}

		for (Int32 i = 0; i < pcnt; ++i)
		{
			if (vertexNormals[i].GetSquaredLength() > 1e-12)
				vertexNormals[i].Normalize();
			else
				vertexNormals[i] = Vector(0.0, 1.0, 0.0);
		}
	}

	// Project vertices in parallel across all CPU cores
	maxon::AtomicBool breakRequested(false);

	maxon::ParallelFor::Dynamic(0, pcnt, [&](Int32 i)
	{
		if (breakRequested.LoadRelaxed())
			return;

		if (thread && (i & 255) == 0 && thread->TestBreak())
		{
			breakRequested.StoreRelaxed(true);
			return;
		}

		Float s = strength;
		if (vmapWeights)
			s *= vmapWeights[i];
		else if (autoWeights)
			s *= autoWeights[i];

		if (s <= 0.0)
			return;

		Vector pLocal = padr[i];
		Vector pWorld = op_mg * pLocal;
		Vector pTargetLocal = invTargetMg * pWorld;

		auto querySurfacePoint = [&](Vector& outHitTargetLocal, Vector& outNormTargetLocal) -> Bool
		{
			if (!snapToVertsEdges && hasCamera)
			{
				Vector rayDirWorld = isPerspective ? (pWorld - camPos) : camForward;
				if (rayDirWorld.GetSquaredLength() > 1e-12)
					rayDirWorld.Normalize();
				else
					rayDirWorld = camForward;

				Vector rayDirTargetLocal = invTargetMg.sqmat * rayDirWorld;

				Vector closestHit, closestNorm;
				Float maxRayDist = (falloffRadius > 0.0) ? falloffRadius : MAXRANGE;

				if (_bvh.FindClosestPoint(pTargetLocal, closestHit, closestNorm, maxRayDist))
				{
					Float closestDist = (pTargetLocal - closestHit).GetLength();
					Float allowedRayDist = maxon::Min(closestDist * 3.0 + 10.0, maxRayDist);

					Vector rayHit, rayNorm;
					if (_bvh.Raycast(pTargetLocal, rayDirTargetLocal, rayHit, rayNorm, allowedRayDist, true))
					{
						outHitTargetLocal = rayHit;
						outNormTargetLocal = closestNorm;
						return true;
					}

					outHitTargetLocal = closestHit;
					outNormTargetLocal = closestNorm;
					return true;
				}
				return false;
			}
			return _bvh.FindClosestPoint(pTargetLocal, outHitTargetLocal, outNormTargetLocal);
		};

		switch (mode)
		{
			case SHRINKWRAP_MODE_NEAREST_SURFACE:
			{
				Vector hitTargetLocal, normTargetLocal;
				if (querySurfacePoint(hitTargetLocal, normTargetLocal))
				{
					Vector hitWorld = targetMg * hitTargetLocal;
					Vector normWorld = targetMg.sqmat * normTargetLocal;
					if (normWorld.GetSquaredLength() > 1e-12)
						normWorld.Normalize();
					else
						normWorld = Vector(0.0, 1.0, 0.0);

					if (aboveSurface)
					{
						Float signedDist = Dot(pWorld - hitWorld, normWorld);
						if (signedDist > offset)
							return;
					}

					Vector targetPosWorld = hitWorld + normWorld * offset;
					Float dist = (pWorld - hitWorld).GetLength();
					if (falloffRadius > 0.0 && dist > falloffRadius)
						return;

					Vector finalWorld = Blend(pWorld, targetPosWorld, s);
					padr[i] = invOpMg * finalWorld;
				}
				break;
			}

			case SHRINKWRAP_MODE_PROJECT:
			{
				Vector normLocal = (vertexNormals.GetCount() == pcnt) ? vertexNormals[i] : Vector(0.0, 1.0, 0.0);
				Vector normWorld = op_mg.sqmat * normLocal;
				if (normWorld.GetSquaredLength() > 1e-12)
					normWorld.Normalize();
				else
					normWorld = Vector(0.0, 1.0, 0.0);

				Vector rayDirTargetLocal = invTargetMg.sqmat * normWorld;
				Vector hitTargetLocal, hitNormTargetLocal;
				Float maxRayDist = (falloffRadius > 0.0) ? falloffRadius : MAXRANGE;

				if (_bvh.Raycast(pTargetLocal, rayDirTargetLocal, hitTargetLocal, hitNormTargetLocal, maxRayDist, bidirectional))
				{
					Vector hitWorld = targetMg * hitTargetLocal;
					Vector normWorldTarget = targetMg.sqmat * hitNormTargetLocal;
					if (normWorldTarget.GetSquaredLength() > 1e-12)
						normWorldTarget.Normalize();
					else
						normWorldTarget = normWorld;

					if (aboveSurface)
					{
						Float signedDist = Dot(pWorld - hitWorld, normWorldTarget);
						if (signedDist > offset)
							return;
					}

					Vector targetPosWorld = hitWorld + normWorldTarget * offset;
					Vector finalWorld = Blend(pWorld, targetPosWorld, s);
					padr[i] = invOpMg * finalWorld;
				}
				break;
			}

			case SHRINKWRAP_MODE_NEAREST_VERTEX:
			{
				Vector vertTargetLocal;
				if (_bvh.FindNearestVertex(pTargetLocal, vertTargetLocal))
				{
					Vector vertWorld = targetMg * vertTargetLocal;
					Float dist = (pWorld - vertWorld).GetLength();
					if (falloffRadius > 0.0 && dist > falloffRadius)
						return;

					Vector dir = pWorld - vertWorld;
					if (dir.GetSquaredLength() > 1e-12)
						dir.Normalize();
					else
						dir = Vector(0.0, 1.0, 0.0);

					Vector targetPosWorld = vertWorld + dir * offset;
					Vector finalWorld = Blend(pWorld, targetPosWorld, s);
					padr[i] = invOpMg * finalWorld;
				}
				break;
			}

			case SHRINKWRAP_MODE_TARGET_NORMAL:
			{
				Vector hitTargetLocal, normTargetLocal;
				if (querySurfacePoint(hitTargetLocal, normTargetLocal))
				{
					Vector hitWorld = targetMg * hitTargetLocal;
					Vector normWorld = targetMg.sqmat * normTargetLocal;
					if (normWorld.GetSquaredLength() > 1e-12)
						normWorld.Normalize();
					else
						normWorld = Vector(0.0, 1.0, 0.0);

					Vector v = pWorld - hitWorld;
					Float distToPlane = Dot(v, normWorld);
					Vector projectedToPlane = pWorld - normWorld * distToPlane;

					if (aboveSurface && distToPlane > offset)
						return;

					Vector targetPosWorld = projectedToPlane + normWorld * offset;
					Float dist = (pWorld - targetPosWorld).GetLength();
					if (falloffRadius > 0.0 && dist > falloffRadius)
						return;

					Vector finalWorld = Blend(pWorld, targetPosWorld, s);
					padr[i] = invOpMg * finalWorld;
				}
				break;
			}

			default:
				break;
		}
	});

	DeleteMem(autoWeights);

	Bool autoBake = data.GetBool(SHRINKWRAP_AUTO_BAKE);
	if (autoBake && mod)
	{
		BaseObject* parent = const_cast<BaseObject*>(mod)->GetUp();
		if (parent && parent->IsInstanceOf(Opolygon))
		{
			PolygonObject* realPoly = static_cast<PolygonObject*>(parent);
			if (realPoly != op && realPoly->GetPointCount() == pcnt)
			{
				Vector* realPadr = realPoly->GetPointW();
				if (realPadr)
				{
					CopyMem(padr, realPadr, sizeof(Vector) * pcnt);
				}
			}
		}
	}

	Bool customColor = data.GetBool(SHRINKWRAP_ENABLE_CUSTOM_COLOR, true);
	if (customColor)
	{
		Vector meshColor = data.GetVector(SHRINKWRAP_MESH_COLOR, Vector(0.0, 150.0 / 255.0, 1.0));
		Float faceOpacity = data.GetFloat(SHRINKWRAP_FACE_OPACITY, 0.35);
		if (faceOpacity > 1.0) faceOpacity /= 100.0;
		if (faceOpacity < 0.0) faceOpacity = 0.0;

		ObjectColorProperties defProp;
		defProp.color = meshColor;
		defProp.usecolor = ID_BASEOBJECT_USECOLOR_ALWAYS;
		defProp.xray = (faceOpacity < 0.99);
		op->SetColorProperties(&defProp);
	}

	op->Message(MSG_UPDATE);
	return true;
}

Bool RegisterShrinkWrap()
{
	Bool res = RegisterObjectPlugin(
		PLUGIN_ID_SHRINKWRAP,
		"ShrinkWrap"_s,
		OBJECT_MODIFIER,
		ShrinkWrapDeformer::Alloc,
		"Oshrinkwrap"_s,
		AutoBitmap("shrinkwrap.png"_s),
		0
	);
	if (!res)
	{
		ApplicationOutput("ShrinkWrap: RegisterObjectPlugin failed!"_s);
	}
	else
	{
		ApplicationOutput("ShrinkWrap: Plugin registered successfully."_s);
	}
	return res;
}

namespace cinema
{
Bool PluginStart()
{
	return ::RegisterShrinkWrap();
}

void PluginEnd()
{
}

Bool PluginMessage(Int32 id, void* data)
{
	switch (id)
	{
		case C4DPL_INIT_SYS:
			if (!g_resource.Init())
			{
				ApplicationOutput("ShrinkWrap: g_resource.Init() failed! Make sure res/c4d_symbols.h and res/strings_en-US/c4d_strings.str exist."_s);
				return false;
			}
			ApplicationOutput("ShrinkWrap: g_resource.Init() succeeded."_s);
			return true;
		case C4DMSG_PRIORITY:
			return true;
		case C4DPL_BUILDMENU:
			break;
	}
	return false;
}
}
