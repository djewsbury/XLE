// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

using namespace System;
using namespace System::Collections::Generic;
using namespace System::Drawing;
using System::Runtime::InteropServices::OutAttribute;

namespace GUILayer 
{

        ///////////////////////////////////////////////////////////////
	public enum class PreviewGeometry { Chart, Plane2D, Box, Sphere, Model };

	public ref class PreviewSettings
	{
	public:
		property PreviewGeometry    Geometry;
		property String^            OutputToVisualize;

		static String^ PreviewGeometryToString(PreviewGeometry geo);
		static PreviewGeometry PreviewGeometryFromString(String^ input);
     };

	ref class RawMaterial;

		///////////////////////////////////////////////////////////////
	[System::Runtime::Serialization::DataContract] public ref class NodeGraphMetaData
    {
    public:
        property String^ DefaultsMaterial;
        property String^ PreviewModelFile;
		property RawMaterial^ Material;

        // Restrictions placed on the input variables
        [System::Runtime::Serialization::DataMember] property Dictionary<String^, String^>^ Variables { Dictionary<String^, String^>^ get() { if (!_variables) _variables = gcnew Dictionary<String^, String^>(); return _variables; } }

        // Configuration settings for the output file
        [System::Runtime::Serialization::DataMember] bool HasTechniqueConfig;
        [System::Runtime::Serialization::DataMember] property Dictionary<String^, String^>^ ShaderParameters { Dictionary<String^, String^>^ get() { if (!_shaderParameters) _shaderParameters = gcnew Dictionary<String^, String^>(); return _shaderParameters; } }

		NodeGraphMetaData() { HasTechniqueConfig = false; }

    private:
        Dictionary<String^, String^>^ _variables = nullptr;
        Dictionary<String^, String^>^ _shaderParameters = nullptr;
    };

	ref class NodeGraphFile;

	public ref class NodeGraphPreviewConfiguration
	{
	public:
		NodeGraphFile^		_nodeGraph;
		String^				_subGraphName;
		UInt32				_previewNodeId; 
	};

	ref class TechniqueDelegateWrapper;
	ref class CompiledShaderPatchCollectionWrapper;
	ref class MessageRelayWrapper;
	
	public ref class ShaderGeneratorLayer
	{
	public:
		static void		LoadNodeGraphFile(String^ filename, [Out] NodeGraphFile^% nodeGraph, [Out] NodeGraphMetaData^% context);
        static void		Serialize(System::IO::Stream^ stream, String^ name, NodeGraphFile^ nodeGraphFile, NodeGraphMetaData^ context);

		static TechniqueDelegateWrapper^ MakeShaderPatchAnalysisDelegate(
			PreviewSettings^ settings,
			IEnumerable<KeyValuePair<String^, String^>>^ variableRestrictions,
			MessageRelayWrapper^ logMessages);

		static CompiledShaderPatchCollectionWrapper^ MakeCompiledShaderPatchCollection(
			NodeGraphMetaData^ doc, 
			NodeGraphPreviewConfiguration^ nodeGraphFile,
			MessageRelayWrapper^ logMessages);
	};

}


