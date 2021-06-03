// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "MaterialScaffold.h"
#include "ShaderPatchCollection.h"
#include "../../Assets/AssetsCore.h"
#include "../../Assets/AssetUtils.h"
#include "../../Utility/ParameterBox.h"
#include "../../Utility/MemoryUtils.h"
#include <memory>
#include <vector>
#include <string>

namespace Assets 
{ 
    class DependencyValidation; class DirectorySearchRules; 
	class DependentFileState;
}
namespace Utility { class OutputStreamFormatter; }

namespace RenderCore { namespace Assets
{
	RenderStateSet Merge(RenderStateSet underride, RenderStateSet override);

    /// <summary>Pre-resolved material settings</summary>
    /// Materials are a hierachical set of properties. Each RawMaterial
    /// object can inherit from sub RawMaterials -- and it can either
    /// inherit or override the properties in those sub RawMaterials.
    ///
    /// RawMaterials are intended to be used in tools (for preprocessing 
    /// and material authoring). ResolvedMaterial is the run-time representation.
    ///
    /// During preprocessing, RawMaterials should be resolved down to a 
    /// ResolvedMaterial object (using the Resolve() method). 
    class RawMaterial
    {
    public:
        using AssetName = ::Assets::rstring;
        
        ParameterBox	_resourceBindings;
        ParameterBox	_matParamBox;
        RenderStateSet	_stateSet;
        ParameterBox	_constants;
        
		ShaderPatchCollection _patchCollection;

        std::vector<AssetName> _inherit;

		void					MergeInto(RawMaterial& dest) const; 
		std::vector<AssetName>	ResolveInherited(const ::Assets::DirectorySearchRules& searchRules) const;

		const ::Assets::DependencyValidation&	GetDependencyValidation() const { return _depVal; }
		const ::Assets::DirectorySearchRules&	GetDirectorySearchRules() const { return _searchRules; }

        void SerializeMethod(OutputStreamFormatter& formatter) const;
        
        RawMaterial();
        RawMaterial(
            InputStreamFormatter<utf8>& formatter, 
            const ::Assets::DirectorySearchRules&,
			const ::Assets::DependencyValidation& depVal);
        ~RawMaterial();

		static void ConstructToFuture(
			::Assets::FuturePtr<RawMaterial>&,
			StringSection<::Assets::ResChar> initializer);

    private:
        ::Assets::DependencyValidation _depVal;
		::Assets::DirectorySearchRules _searchRules;
    };

	class RawMatConfigurations
    {
    public:
        std::vector<std::basic_string<utf8>> _configurations;

		RawMatConfigurations(
			const ::Assets::Blob& locator,
			const ::Assets::DependencyValidation& depVal,
			StringSection<::Assets::ResChar> requestParameters);

        static const auto CompileProcessType = ConstHash64<'RawM', 'at'>::Value;

        auto GetDependencyValidation() const -> const ::Assets::DependencyValidation& { return _validationCallback; }
    protected:
        ::Assets::DependencyValidation _validationCallback;
    };

    void ResolveMaterialFilename(
        ::Assets::ResChar resolvedFile[], unsigned resolvedFileCount,
        const ::Assets::DirectorySearchRules& searchRules, StringSection<char> baseMatName);

	MaterialGuid MakeMaterialGuid(StringSection<utf8> name);

	void MergeInto(MaterialScaffoldMaterial& dest, ShaderPatchCollection& patchCollectionResult, const RawMaterial& source); 

	void MergeIn_Stall(
		MaterialScaffoldMaterial& result,
		ShaderPatchCollection& patchCollectionResult,
		StringSection<> sourceMaterialName,
		const ::Assets::DirectorySearchRules& searchRules,
		std::vector<::Assets::DependentFileState>& deps);

	void MergeIn_Stall(
		MaterialScaffoldMaterial& result,
		ShaderPatchCollection& patchCollectionResult,
		const RawMaterial& src,
		const ::Assets::DirectorySearchRules& searchRules);


}}

