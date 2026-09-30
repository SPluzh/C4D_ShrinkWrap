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

static const PolygonObject* FindPolygonRecursive(const BaseObject* op)
{
	while (op)
	{
		if (op->IsInstanceOf(Opolygon))
			return static_cast<const PolygonObject*>(op);

		if (op->GetDeformCache())
		{
			const PolygonObject* p = FindPolygonRecursive(op->GetDeformCache());
			if (p)
				return p;
		}

		if (op->GetCache())
		{
			const PolygonObject* p = FindPolygonRecursive(op->GetCache());
			if (p)
				return p;
		}

		if (op->GetDown())
		{
			const PolygonObject* p = FindPolygonRecursive(op->GetDown());
			if (p)
				return p;
		}

		op = op->GetNext();
	}
	return nullptr;
}

static const PolygonObject* GetTargetPolygonObject(const BaseObject* op)
{
	if (!op)
		return nullptr;

	if (op->IsInstanceOf(Opolygon))
		return static_cast<const PolygonObject*>(op);

	if (op->GetDeformCache())
	{
		const PolygonObject* p = FindPolygonRecursive(op->GetDeformCache());
		if (p)
			return p;
	}

	if (op->GetCache())
	{
		const PolygonObject* p = FindPolygonRecursive(op->GetCache());
		if (p)
			return p;
	}

	if (op->GetDown())
	{
		const PolygonObject* p = FindPolygonRecursive(op->GetDown());
		if (p)
			return p;
	}

	return nullptr;
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

static void ApplyMeshDisplay(BaseObject* deformer)
{
	if (!deformer)
		return;

	BaseObject* parent = deformer->GetUp();
	if (!parent)
		return;

	const BaseContainer& data = deformer->GetDataInstanceRef();
	Bool customColor = data.GetBool(SHRINKWRAP_ENABLE_CUSTOM_COLOR, true);
	Vector meshColor = data.GetVector(SHRINKWRAP_MESH_COLOR, Vector(0.0, 150.0 / 255.0, 1.0));
	Bool xray = data.GetBool(SHRINKWRAP_MESH_XRAY, true);
	Bool shouldApply = customColor && deformer->GetDeformMode();

	BaseContainer& parentData = parent->GetDataInstanceRef();

	if (shouldApply)
	{
		Bool changed = false;
		if (parentData.GetInt32(ID_BASEOBJECT_USECOLOR) != ID_BASEOBJECT_USECOLOR_ALWAYS)
		{
			parentData.SetInt32(ID_BASEOBJECT_USECOLOR, ID_BASEOBJECT_USECOLOR_ALWAYS);
			parent->SetParameter(DescID::Create((Int32)ID_BASEOBJECT_USECOLOR), GeData((Int32)ID_BASEOBJECT_USECOLOR_ALWAYS), DESCFLAGS_SET::NONE);
			changed = true;
		}
		if (parentData.GetVector(ID_BASEOBJECT_COLOR) != meshColor)
		{
			parentData.SetVector(ID_BASEOBJECT_COLOR, meshColor);
			parent->SetParameter(DescID::Create((Int32)ID_BASEOBJECT_COLOR), GeData(meshColor), DESCFLAGS_SET::NONE);
			changed = true;
		}
		if (parentData.GetBool(ID_BASEOBJECT_XRAY) != xray)
		{
			parentData.SetBool(ID_BASEOBJECT_XRAY, xray);
			parent->SetParameter(DescID::Create((Int32)ID_BASEOBJECT_XRAY), GeData(xray), DESCFLAGS_SET::NONE);
			changed = true;
		}
		if (changed)
		{
			parent->Message(MSG_UPDATE);
		}
	}
	else
	{
		Bool changed = false;
		if (parentData.GetInt32(ID_BASEOBJECT_USECOLOR) == ID_BASEOBJECT_USECOLOR_ALWAYS)
		{
			parentData.SetInt32(ID_BASEOBJECT_USECOLOR, ID_BASEOBJECT_USECOLOR_OFF);
			parent->SetParameter(DescID::Create((Int32)ID_BASEOBJECT_USECOLOR), GeData((Int32)ID_BASEOBJECT_USECOLOR_OFF), DESCFLAGS_SET::NONE);
			changed = true;
		}
		if (parentData.GetBool(ID_BASEOBJECT_XRAY))
		{
			parentData.SetBool(ID_BASEOBJECT_XRAY, false);
			parent->SetParameter(DescID::Create((Int32)ID_BASEOBJECT_XRAY), GeData(false), DESCFLAGS_SET::NONE);
			changed = true;
		}
		if (changed)
		{
			parent->Message(MSG_UPDATE);
		}
	}
}

Bool ShrinkWrapDeformer::Init(GeListNode* node, Bool isCloneInit)
{
	if (!node)
		return false;

	if (!isCloneInit)
	{
		BaseContainer& data = static_cast<BaseObject*>(node)->GetDataInstanceRef();
		data.SetInt32(SHRINKWRAP_MODE, SHRINKWRAP_MODE_NEAREST_SURFACE);
		data.SetFloat(SHRINKWRAP_OFFSET, 0.0);
		data.SetFloat(SHRINKWRAP_STRENGTH, 1.0);
		data.SetFloat(SHRINKWRAP_FALLOFF_RADIUS, 0.0);
		data.SetBool(SHRINKWRAP_BIDIRECTIONAL, true);
		data.SetBool(SHRINKWRAP_ABOVE_SURFACE, false);
		data.SetBool(SHRINKWRAP_AUTO_BAKE, true);

		// QuadDraw Retopo Display styling defaults
		data.SetBool(SHRINKWRAP_ENABLE_CUSTOM_COLOR, true);
		data.SetVector(SHRINKWRAP_MESH_COLOR, Vector(0.0, 150.0 / 255.0, 1.0)); // QuadDraw Cyan (0, 150, 255)
		data.SetBool(SHRINKWRAP_MESH_XRAY, true);
		data.SetBool(SHRINKWRAP_DRAW_WIREFRAME, true);
		data.SetVector(SHRINKWRAP_WIRE_COLOR, Vector(0.0, 0.0, 0.0)); // Crisp black wireframe
		data.SetFloat(SHRINKWRAP_WIRE_WIDTH, 1.5);
	}

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
		ApplyMeshDisplay(op);
		EventAdd();
	}
	else if (type == MSG_DESCRIPTION_CHECKUPDATE)
	{
		if (doc)
		{
			EnsureDeformedEditing(doc);
		}
		ApplyMeshDisplay(op);
		EventAdd();
	}
	else if (type == MSG_DESCRIPTION_POSTSETPARAMETER)
	{
		ApplyMeshDisplay(op);
		EventAdd();
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

	ApplyMeshDisplay(op);

	const BaseContainer& data = op->GetDataInstanceRef();
	const BaseObject* targetObj = data.GetObjectLink(SHRINKWRAP_TARGET_LINK, doc);
	if (targetObj)
	{
		UInt64 tDirty = targetObj->GetDirty(DIRTYFLAGS::DATA | DIRTYFLAGS::MATRIX);
		if (tDirty != _cachedTargetObjDirty)
		{
			_cachedTargetObjDirty = tDirty;
			op->SetDirty(DIRTYFLAGS::DATA);
		}
	}
}

DRAWRESULT ShrinkWrapDeformer::Draw(BaseObject* op, DRAWPASS drawpass, BaseDraw* bd, BaseDrawHelp* bh)
{
	if (!op || !bd || !bh)
		return DRAWRESULT::OK;

	if (drawpass != DRAWPASS::OBJECT)
		return DRAWRESULT::OK;

	if (!op->GetDeformMode())
		return DRAWRESULT::OK;

	const BaseContainer& data = op->GetDataInstanceRef();
	if (!data.GetBool(SHRINKWRAP_DRAW_WIREFRAME, true))
		return DRAWRESULT::OK;

	BaseObject* parent = op->GetUp();
	if (!parent)
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

	Vector wireColor = data.GetVector(SHRINKWRAP_WIRE_COLOR, Vector(0.0, 0.0, 0.0));
	Float lineWidth = data.GetFloat(SHRINKWRAP_WIRE_WIDTH, 1.5);
	if (lineWidth < 1.0) lineWidth = 1.0;
	if (lineWidth > 10.0) lineWidth = 10.0;

	GeData oldLineWidth = bd->GetDrawParam(DRAW_PARAMETER_LINEWIDTH);
	bd->SetDrawParam(DRAW_PARAMETER_LINEWIDTH, GeData(lineWidth));
	bd->SetPen(wireColor);

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

	ApplyMeshDisplay(const_cast<BaseObject*>(mod));

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

	Float strength = data.GetFloat(SHRINKWRAP_STRENGTH);
	if (strength <= 0.0)
		return true;

	PointObject* ptOp = ToPoint(op);
	Vector* padr = ptOp->GetPointW();
	Int32 pcnt = ptOp->GetPointCount();
	if (!padr || pcnt <= 0)
		return true;

	const PolygonObject* targetPoly = GetTargetPolygonObject(targetObj);
	if (!targetPoly)
		return true;

	// BVH Caching with dirty check
	UInt64 curDirty = targetPoly->GetDirty(DIRTYFLAGS::DATA);
	if (!_bvh.IsBuilt() || curDirty != _cachedTargetDirty || targetPoly != _cachedTargetPtr ||
		targetPoly->GetPolygonCount() != _cachedTargetPolyCount ||
		targetPoly->GetPointCount() != _cachedTargetPointCount)
	{
		auto t0 = std::chrono::high_resolution_clock::now();
		if (!_bvh.Build(targetPoly, thread))
			return true;
		auto t1 = std::chrono::high_resolution_clock::now();
		double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
		ApplicationOutput("ShrinkWrap: BVH built for @ polygons (@ triangles) in @ ms."_s,
						  targetPoly->GetPolygonCount(), _bvh.GetTriangleCount(), ms);

		_cachedTargetDirty = curDirty;
		_cachedTargetPtr = targetPoly;
		_cachedTargetPolyCount = targetPoly->GetPolygonCount();
		_cachedTargetPointCount = targetPoly->GetPointCount();
	}

	if (!_bvh.IsBuilt())
		return true;

	Int32 mode = data.GetInt32(SHRINKWRAP_MODE);
	Float offset = data.GetFloat(SHRINKWRAP_OFFSET);
	Float falloffRadius = data.GetFloat(SHRINKWRAP_FALLOFF_RADIUS);
	Bool bidirectional = data.GetBool(SHRINKWRAP_BIDIRECTIONAL);
	Bool aboveSurface = data.GetBool(SHRINKWRAP_ABOVE_SURFACE);

	Matrix targetMg = targetObj->GetMg();
	Matrix invTargetMg = ~targetMg;
	Matrix invOpMg = ~op_mg;

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

		switch (mode)
		{
			case SHRINKWRAP_MODE_NEAREST_SURFACE:
			{
				Vector hitTargetLocal, normTargetLocal;
				if (_bvh.FindClosestPoint(pTargetLocal, hitTargetLocal, normTargetLocal))
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
				if (_bvh.FindClosestPoint(pTargetLocal, hitTargetLocal, normTargetLocal))
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
