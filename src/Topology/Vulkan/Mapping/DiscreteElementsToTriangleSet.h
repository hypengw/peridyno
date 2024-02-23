#pragma once
#include "Module/TopologyMapping.h"

#include "Topology/DiscreteElements.h"
#include "Topology/TriangleSet.h"

namespace dyno
{
	template<typename TDataType>
	class DiscreteElementsToTriangleSet : public TopologyMapping
	{
		DECLARE_TCLASS(DiscreteElementsToTriangleSet, TDataType);
	public:
		DiscreteElementsToTriangleSet();

	protected:
		bool apply() override;

	public:
		DEF_INSTANCE_IN(DiscreteElements, DiscreteElements, "");
		DEF_INSTANCE_OUT(TriangleSet<TDataType>, TriangleSet, "");

	private:
		TriangleSet<TDataType> mStandardSphere;
		TriangleSet<TDataType> mStandardCapsule;
	};
}