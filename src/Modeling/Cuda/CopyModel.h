/**
 * Copyright 2022 Yuzhong Guo
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once
#include "Node/ParametricModel.h"

#include "Topology/TriangleSet.h"

namespace dyno
{
	template<typename TDataType>
	class CopyModel : public ParametricModel<TDataType>
	{
		DECLARE_TCLASS(CopyModel, TDataType);

	public:
		typedef typename TDataType::Real Real;
		typedef typename TDataType::Coord Coord;

		CopyModel();

		DECLARE_ENUM(ScaleMode,
			Power = 0,
			Multiply = 1);

	public:
		DEF_VAR(unsigned, TotalNumber, 3, "CopyNumber");

		DEF_VAR(Coord, CopyTransform, 0, "CopyTransform");

		DEF_VAR(Coord, CopyRotation, 0, "CopyRotation");

		DEF_VAR(Coord, CopyScale, 1, "CopyScale");

		DEF_ENUM(ScaleMode, ScaleMode, ScaleMode::Power, "ScaleMode");

		DEF_INSTANCE_STATE(TriangleSet<TDataType>, TriangleSet, "");

		DEF_INSTANCE_IN(TriangleSet<TDataType>, TriangleSetIn,"")

	protected:
		void resetStates() override;
	};

	IMPLEMENT_TCLASS(CopyModel, TDataType);
}