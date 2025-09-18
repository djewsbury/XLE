// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/IteratorUtils.h"
#include "../Utility/Streams/SerializationUtils.h"

namespace Assets { class BlockSerializer; }

namespace GraphLanguage
{
	enum class ParameterDirection : uint32_t { In, Out };
	class NodeGraphSignature
	{
	public:
		struct Parameter
		{
			SerializableString _type, _name;
			ParameterDirection _direction = ParameterDirection::In;
			SerializableString _semantic, _default;
		};

		// Returns the list of parameters taken as input through the function call mechanism
		auto GetParameters() const -> IteratorRange<const Parameter*>	{ return _functionParameters; }
		auto GetParameters() -> IteratorRange<Parameter*>				{ return MakeIteratorRange(_functionParameters); }
		void AddParameter(const Parameter& param);

		// Returns the list of parameters that are accesses as global scope variables (or captured from a containing scope)
		// In other words, these aren't explicitly passed to the function, but the function needs to interact with them, anyway
		auto GetCapturedParameters() const -> IteratorRange<const Parameter*>		{ return _capturedParameters; }
		void AddCapturedParameter(const Parameter& param);

		struct TemplateParameter
		{
			SerializableString _name;
			SerializableString _restriction;
		};
		auto GetTemplateParameters() const -> IteratorRange<const TemplateParameter*>   { return _templateParameters; }
		void AddTemplateParameter(const TemplateParameter& param);

		const SerializableString& GetImplements() const { return _implements; }
		void SetImplements(const SerializableString& value) { _implements = value; }
		
		NodeGraphSignature();
		~NodeGraphSignature();

		friend void SerializationOperator(::Assets::BlockSerializer&, const NodeGraphSignature&);
	private:
		SerializableVector<Parameter> _functionParameters;
		SerializableVector<Parameter> _capturedParameters;
		SerializableVector<TemplateParameter> _templateParameters;
		SerializableString _implements;
	};

	class UniformBufferSignature
	{
	public:
		struct Parameter
		{
			SerializableString _name;
			SerializableString _type;
			SerializableString _semantic;
		};

		SerializableVector<Parameter> _parameters;

		friend void SerializationOperator(::Assets::BlockSerializer&, const UniformBufferSignature&);
	};

	#pragma pack(push)
	#pragma pack(1)				// we need to prevent packing within the pair

	class ShaderFragmentSignature
	{
	public:
		SerializableVector<std::pair<SerializableString, NodeGraphSignature>>			_functions;
		SerializableVector<std::pair<SerializableString, UniformBufferSignature>>		_uniformBuffers;

		friend void SerializationOperator(::Assets::BlockSerializer&, const ShaderFragmentSignature&);
	};

	#pragma pack(pop)
}

