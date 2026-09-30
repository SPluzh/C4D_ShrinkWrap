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
	virtual maxon::Bool Init(cinema::GeListNode* node, maxon::Bool isCloneInit);
	virtual maxon::Bool Message(cinema::GeListNode* node, maxon::Int32 type, void* data);
	virtual void CheckDirty(cinema::BaseObject* op, const cinema::BaseDocument* doc);
	virtual cinema::DRAWRESULT Draw(cinema::BaseObject* op, cinema::DRAWPASS drawpass, cinema::BaseDraw* bd, cinema::BaseDrawHelp* bh);
	virtual maxon::Bool ModifyObject(const cinema::BaseObject* mod, const cinema::BaseDocument* doc,
									 cinema::BaseObject* op, const cinema::Matrix& op_mg,
									 const cinema::Matrix& mod_mg, maxon::Float lod,
									 maxon::Int32 flags, cinema::BaseThread* thread) const;
	virtual void GetDimension(const cinema::BaseObject* op, cinema::Vector* mp, cinema::Vector* rad) const;
	virtual maxon::Result<maxon::Bool> GetAccessedObjects(const cinema::BaseList2D* node,
														  cinema::METHOD_ID method,
														  cinema::AccessedObjectsCallback& access) const;

	static cinema::NodeData* Alloc() { return NewObjClear(ShrinkWrapDeformer); }

private:
	mutable cinema::TriangleBVH _bvh;
	mutable maxon::UInt64 _cachedTargetDirty = 0;
	mutable maxon::UInt64 _cachedTargetObjDirty = 0;
	mutable const cinema::BaseObject* _cachedTargetPtr = nullptr;
	mutable maxon::Int32 _cachedTargetPolyCount = 0;
	mutable maxon::Int32 _cachedTargetPointCount = 0;
};

maxon::Bool RegisterShrinkWrap();

#endif // SHRINKWRAP_H__
