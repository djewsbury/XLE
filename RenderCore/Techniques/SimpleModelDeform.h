// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Types.h"
#include "../VertexUtil.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Utility/StringUtils.h"
#include <vector>
#include <functional>

namespace RenderCore { namespace Assets { class ModelScaffold; }}

namespace RenderCore { namespace Techniques 
{
	class IDeformOperation
	{
	public:
		using VertexElementRange = IteratorRange<RenderCore::VertexElementIterator>;
		virtual void Execute(
			IteratorRange<const VertexElementRange*> sourceElements,
			IteratorRange<const VertexElementRange*> destinationElements) const = 0;
		virtual ~IDeformOperation();
	};

	class DeformOperationInstantiation
	{
	public:
		std::shared_ptr<IDeformOperation> _operation;
		unsigned _geoId = ~0u;
		struct NameAndFormat { std::string _semantic; unsigned _semanticIndex; Format _format = Format(0); };
		std::vector<NameAndFormat> _generatedElements;				///< these are new elements generated by the deform operation
		std::vector<NameAndFormat> _upstreamSourceElements;			///< these are elements that are requested from some upstream source (either a previous deform operation or the static data)
		std::vector<uint64_t> _suppressElements;					///< hide these elements from downstream 

		friend bool operator==(const NameAndFormat& lhs, const NameAndFormat& rhs)
		{
			return (lhs._semantic == rhs._semantic) && (lhs._semanticIndex == rhs._semanticIndex) && (lhs._format == rhs._format);
		}
	};

	class DeformOperationFactory
	{
	public:
		using InstantiationSet = std::vector<DeformOperationInstantiation>;

		InstantiationSet CreateDeformOperations(
			StringSection<> initializer,
			const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold);

		using RegisteredDeformId = uint32_t;

		using InitiationFunction = std::function<InstantiationSet(StringSection<>, const std::shared_ptr<RenderCore::Assets::ModelScaffold>&)>;
		RegisteredDeformId RegisterDeformOperation(StringSection<> name, InitiationFunction&& fn);
		void DeregisterDeformOperation(RegisteredDeformId);
		
		static DeformOperationFactory& GetInstance();

		DeformOperationFactory();
		~DeformOperationFactory();
	private:
		struct RegisteredDeformOp
		{
			InitiationFunction _instFunction;
			RegisteredDeformId _deformId;
		};
		std::vector<std::pair<uint64_t, RegisteredDeformOp>> _instantiationFunctions;
		unsigned _nextDeformId;
	};

}}
