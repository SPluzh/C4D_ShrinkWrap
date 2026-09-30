#ifndef SHRINKWRAP_H__
#define SHRINKWRAP_H__

#include "c4d.h"
#include "c4d_objectdata.h"
#include "shrinkwrap_bvh.h"
#include "oshrinkwrap.h"

#define PLUGIN_ID_SHRINKWRAP 1068200

class ShrinkWrapDeformer : public cinema::ObjectData
{
public:
	virtual ~ShrinkWrapDeformer();
	virtual void Free(cinema::GeListNode* node) override;
	virtual maxon::Bool Init(cinema::GeListNode* node, maxon::Bool isCloneInit) override;
	virtual maxon::Bool CopyTo(cinema::NodeData* dest, const cinema::GeListNode* snode, cinema::GeListNode* dnode, cinema::COPYFLAGS flags, cinema::AliasTrans* trn) const override;
	virtual maxon::Bool Message(cinema::GeListNode* node, maxon::Int32 type, void* data) override;
	virtual void CheckDirty(cinema::BaseObject* op, const cinema::BaseDocument* doc) override;
	virtual cinema::DRAWRESULT Draw(cinema::BaseObject* op, cinema::DRAWPASS drawpass, cinema::BaseDraw* bd, cinema::BaseDrawHelp* bh) override;
	virtual maxon::Bool ModifyObject(const cinema::BaseObject* mod, const cinema::BaseDocument* doc,
									 cinema::BaseObject* op, const cinema::Matrix& op_mg,
									 const cinema::Matrix& mod_mg, maxon::Float lod,
									 maxon::Int32 flags, cinema::BaseThread* thread) const override;
	virtual void GetDimension(const cinema::BaseObject* op, cinema::Vector* mp, cinema::Vector* rad) const override;
	virtual maxon::Result<maxon::Bool> GetAccessedObjects(const cinema::BaseList2D* node,
														  cinema::METHOD_ID method,
														  cinema::AccessedObjectsCallback& access) const override;

	static cinema::NodeData* Alloc() { return NewObjClear(ShrinkWrapDeformer); }

private:
	void SyncMeshDisplay(cinema::BaseObject* deformer, maxon::Bool forceRestore = false) const;

	mutable cinema::TriangleBVH _bvh;
	// Hierarchy dirty tracking for CheckDirty
	mutable maxon::UInt64 _checkDirtyHash = 0;
	mutable const cinema::BaseObject* _checkDirtyTargetRoot = nullptr;

	// Hierarchy cache tracking for ModifyObject (BVH rebuild)
	mutable maxon::UInt64 _cachedHierarchyDirty = 0;
	mutable const cinema::BaseObject* _cachedTargetRoot = nullptr;
	mutable maxon::Int32 _cachedObjectCount = 0;
	mutable maxon::Int32 _cachedTotalPolyCount = 0;
	mutable maxon::Int32 _cachedTotalPointCount = 0;

	// Parent mesh display restoration tracking
	mutable maxon::Bool _meshDisplayApplied = false;
	mutable cinema::AutoAlloc<cinema::BaseLink> _lastParentLink;
	mutable cinema::Vector _origParentColor = cinema::Vector(0.0);
	mutable maxon::Int32 _origParentUseColor = 0;
	mutable maxon::Bool _origParentXray = false;
};

maxon::Bool RegisterShrinkWrap();

#endif // SHRINKWRAP_H__
