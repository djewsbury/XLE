// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Browser.h"
#include "../Font.h"
#include "../../xleres/FileList.h"

#include "../../../Tools/ToolsRig/ModelVisualisation.h"

#include "../../RenderCore/ResourceDesc.h"
#include "../../RenderCore/Metal/State.h"
#include "../../RenderCore/Metal/InputLayout.h"
#include "../../RenderCore/Metal/Shader.h"
#include "../../RenderCore/Metal/DeviceContext.h"
#include "../../RenderCore/Metal/TextureView.h"
#include "../../RenderCore/Metal/ObjectFactory.h"
#include "../../FixedFunctionModel/SharedStateSet.h"
#include "../../FixedFunctionModel/ModelCache.h"
#include "../../RenderCore/Techniques/DeferredShaderResource.h"
#include "../../RenderCore/Assets/Services.h"
#include "../../RenderCore/Metal/DeviceContextImpl.h"
#include "../../RenderCore/IThreadContext.h"
#include "../../RenderCore/Types.h"
#include "../../RenderCore/Format.h"
#include "../../RenderCore/BufferView.h"

// #include "../../SceneEngine/SceneEngineUtils.h"
#include "../../SceneEngine/LightDesc.h"
#include "../../SceneEngine/LightingParser.h"
#include "../../SceneEngine/LightingParserContext.h"
#include "../../SceneEngine/SceneParser.h"
#include "../../SceneEngine/PreparedScene.h"
#include "../../RenderCore/Techniques/TechniqueUtils.h"
#include "../../RenderCore/Techniques/Techniques.h"
#include "../../RenderCore/Techniques/ParsingContext.h"

#include "../../Assets/AssetUtils.h"
#include "../../Assets/Assets.h"
#include "../../BufferUploads/IBufferUploads.h"
#include "../../BufferUploads/ResourceLocator.h"
#include "../../Math/Transformations.h"

#include "../../Utility/StringUtils.h"
#include "../../Utility/UTFUtils.h"
#include "../../Utility/PtrUtils.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/HeapUtils.h"
#include "../../Utility/Streams/PathUtils.h"

#include "../../OSServices/WinAPI/IncludeWindows.h"

#pragma warning(disable:4505) //  'Overlays::Copy2DTexture' : unreferenced local function has been removed

namespace Overlays
{
    using namespace RenderCore;
    typedef std::basic_string<ucs2> ucs2string;
    
    class DirectoryQuery
    {
    public:
        class Entry 
        {
        public:
            ucs2string  _filename;
        };
        std::vector<Entry> _files;
        std::vector<Entry> _directories;

        DirectoryQuery(const char directoryName[]);
        ~DirectoryQuery();
    };

    DirectoryQuery::DirectoryQuery(const char inputQuery[])
    {
            // read the contents of this directory, and fill in the "_entries" vector
        ucs2 queryString[MaxPath];
        utf8_2_ucs2((const utf8*)inputQuery, XlStringSize(inputQuery), queryString, dimof(queryString));

        ucs2 baseDirectory[MaxPath];
        XlDirname(baseDirectory, dimof(baseDirectory), queryString);

        std::vector<Entry> files;
        std::vector<Entry> directories;

            // using Win32 api interface directly
        WIN32_FIND_DATAW findData;
        XlZeroMemory(findData);
        HANDLE findHandle = FindFirstFileW((LPCWSTR)queryString, &findData);
        if (findHandle != INVALID_HANDLE_VALUE) {
            do {
                    // hide things that begin with "." -- hidden files or system directories
                if (findData.cFileName[0] != L'.') {
                    Entry result;
                    result._filename = ucs2string(baseDirectory) + ((ucs2*)findData.cFileName);
                    if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                        directories.push_back(result);
                    } else {
                        files.push_back(result);
                    }
                }
            } while (FindNextFileW(findHandle, &findData));
            FindClose(findHandle);
        }

        _files = std::move(files);
        _directories = std::move(directories);
    }

    DirectoryQuery::~DirectoryQuery() {}
    
////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class SharedBrowser::Pimpl
    {
    public:
        std::unique_ptr<DirectoryQuery> _subDirectories;
        std::unique_ptr<DirectoryQuery> _modelFiles;
        std::string     _baseDirectory;
        std::string     _currentDirectory;
		std::shared_ptr<Font> _headingFont;
        unsigned        _itemDimensions;
        std::string     _fileFilter;
        std::string     _headerName;

        Pimpl() {}
    };

	template<typename Type>
		RenderCore::Metal::Buffer MakeConstantBuffer(const Type& obj)
		{
			return RenderCore::Metal::MakeConstantBuffer(
				Metal::GetObjectFactory(),
				MakeIteratorRange(&obj, PtrAdd(&obj, sizeof(Type))));
		}

    static void Copy2DTexture(
        RenderCore::Metal::DeviceContext* context, const RenderCore::Metal::ShaderResourceView& srv, Float2 screenMins, Float2 screenMaxs, float scrollAreaMin, float scrollAreaMax)
    {
        using namespace RenderCore::Metal;
        
        class Vertex
        {
        public:
            Float2  _position;
            Float2  _texCoord;
        } vertices[] = {
            { Float2(screenMins[0], screenMins[1]), Float2(0.f, 0.f) },
            { Float2(screenMins[0], screenMaxs[1]), Float2(0.f, 1.f) },
            { Float2(screenMaxs[0], screenMins[1]), Float2(1.f, 0.f) },
            { Float2(screenMaxs[0], screenMaxs[1]), Float2(1.f, 1.f) }
        };

        InputElementDesc vertexInputLayout[] = {
            InputElementDesc( "POSITION", 0, Format::R32G32_FLOAT ),
            InputElementDesc( "TEXCOORD", 0, Format::R32G32_FLOAT )
        };

        auto vertexBuffer = MakeVertexBuffer(GetObjectFactory(), MakeIteratorRange(vertices));

        const auto& shaderProgram = ::Assets::Legacy::GetAssetDep<ShaderProgram>(
            BASIC2D_VERTEX_HLSL ":P2T:" VS_DefShaderModel, 
            BASIC_PIXEL_HLSL ":copy_point_scrolllimit:" PS_DefShaderModel);
        BoundInputLayout boundVertexInputLayout(MakeIteratorRange(vertexInputLayout), shaderProgram);
		VertexBufferView vbvs[] = { VertexBufferView{&vertexBuffer} };
        boundVertexInputLayout.Apply(*context, MakeIteratorRange(vbvs));
        context->Bind(shaderProgram);

        ViewportDesc viewport = context->GetBoundViewport();
        float constants[] = { 1.f / viewport.Width, 1.f / viewport.Height, 0.f, 0.f };
        float scrollConstants[] = { scrollAreaMin, scrollAreaMax, 0.f, 0.f };
        auto reciprocalViewportDimensions = MakeConstantBuffer(constants);
        auto scrollConstantsBuffer = MakeConstantBuffer(scrollConstants);
        const ShaderResourceView* resources[] = { &srv };
		ConstantBufferView cnsts[] = { {&reciprocalViewportDimensions}, {&scrollConstantsBuffer} };
		UniformsStreamInterface interf;
		interf.BindConstantBuffer(0, { Hash64("ReciprocalViewportDimensionsCB") });
		interf.BindConstantBuffer(1, { Hash64("ScrollConstants") });
		interf.BindShaderResource(0, { Hash64("DiffuseTexture") });

		BoundUniforms boundLayout(
			shaderProgram,
			PipelineLayoutConfig{},
			UniformsStreamInterface{},
			interf);
        
		boundLayout.Apply(
			*context, 1,
			{
				MakeIteratorRange(cnsts),
				UniformsStream::MakeResources(MakeIteratorRange(resources))
			});

        context->Bind(BlendState(BlendOp::Add, Blend::SrcAlpha, Blend::InvSrcAlpha));
        context->Bind(Topology::TriangleStrip);
        context->Draw(dimof(vertices));

		boundLayout.UnbindShaderResources(*context, 0);
    }

    class ButtonFormatting
    {
    public:
        ColorB  _background;
        ColorB  _foreground;
        ButtonFormatting(ColorB background, ColorB foreground) : _background(background), _foreground(foreground) {}
    };

    static void DrawButtonBasic(IOverlayContext& context, const Rect& rect, const char label[], ButtonFormatting formatting)
    {
        FillRectangle(context, rect, formatting._background);
        OutlineRectangle(context, rect, 0.f, formatting._foreground);
        // DrawText(context, rect, 0.f, nullptr, formatting._foreground, manipulatorName);
        context.DrawText(
            std::make_tuple(Float3(float(rect._topLeft[0]), float(rect._topLeft[1]), 0.f), Float3(float(rect._bottomRight[0]), float(rect._bottomRight[1]), 0.f)),
			GetDefaultFont(), TextStyle{}, formatting._foreground, TextAlignment::Center, label);
    }

    template<typename T> inline const T& FormatButton(InterfaceState& interfaceState, InteractableId id, const T& normalState, const T& mouseOverState, const T& pressedState)
    {
        if (interfaceState.HasMouseOver(id))
            return interfaceState.IsMouseButtonHeld(0)?pressedState:mouseOverState;
        return normalState;
    }

    static const auto Id_TotalRect = InteractableId_Make("ModelBrowser");
    static const auto Id_Directories = InteractableId_Make("BrowserDirectories");
    static const auto Id_BackDirectory = InteractableId_Make("BrowserBackDirectory");
    static const auto Id_MainSurface = InteractableId_Make("BrowserMainSurface");
    static const auto Id_MainScroller = InteractableId_Make("BrowserScroller");

    void    SharedBrowser::Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState)
    {
        if (!_pimpl->_subDirectories) {
            _pimpl->_subDirectories = std::make_unique<DirectoryQuery>((_pimpl->_currentDirectory + "\\*.*").c_str());
        }

        if (!_pimpl->_modelFiles) {
            _pimpl->_modelFiles = std::make_unique<DirectoryQuery>((_pimpl->_currentDirectory + "\\" + _pimpl->_fileFilter).c_str());
        }

        static float desiredWidthPercentage = 50.f/100.f;
        static ColorB backgroundRectangleColour(64, 96, 64, 127);
        static ColorB backgroundOutlineColour(192, 192, 192, 0xff);
        static ColorB headerColourNormal(192, 192, 192, 0xff);
        static ColorB headerColourHighlight(0xff, 0xff, 0xff, 0xff);

        static ButtonFormatting buttonNormalState(ColorB(127, 192, 127, 64), ColorB(164, 192, 164, 255));
        static ButtonFormatting buttonMouseOverState(ColorB(127, 192, 127, 64), ColorB(255, 255, 255, 160));
        static ButtonFormatting buttonPressedState(ColorB(127, 192, 127, 64), ColorB(255, 255, 255, 96));

        Layout toolBoxLayout(layout.AllocateFullHeightFraction(desiredWidthPercentage));

            // Header at the top
        {
            FillRectangle(&context, toolBoxLayout.GetMaximumSize(), backgroundRectangleColour);
            OutlineRectangle(&context, Rect(toolBoxLayout.GetMaximumSize()._topLeft + Coord2(2,2), toolBoxLayout.GetMaximumSize()._bottomRight - Coord2(2,2)), 0.f, backgroundOutlineColour);
            interactables.Register(Interactables::Widget(toolBoxLayout.GetMaximumSize(), Id_TotalRect));

            const auto headingRect = toolBoxLayout.AllocateFullWidth(25);
            context.DrawText(
                std::make_tuple(Float3(float(headingRect._topLeft[0]), float(headingRect._topLeft[1]), 0.f), Float3(float(headingRect._bottomRight[0]), float(headingRect._bottomRight[1]), 0.f)),
				_pimpl->_headingFont, TextStyle{}, interfaceState.HasMouseOver(Id_TotalRect)?headerColourHighlight:headerColourNormal, TextAlignment::Center, 
                    _pimpl->_headerName.c_str());
        }

            //  Write the current directory name
        unsigned textHeight = 8 + (unsigned)GetDefaultFont()->GetFontProperties()._lineHeight;
        auto curDirRect = toolBoxLayout.AllocateFullWidth(textHeight);
        context.DrawText(
            std::make_tuple(Float3(float(curDirRect._topLeft[0]), float(curDirRect._topLeft[1]), 0.f), Float3(float(curDirRect._bottomRight[0]), float(curDirRect._bottomRight[1]), 0.f)),
			GetDefaultFont(), TextStyle{}, headerColourNormal, TextAlignment::Center, _pimpl->_currentDirectory.c_str());

        {
            auto border = toolBoxLayout.AllocateFullWidth(2); // small border to reset current line
            context.DrawLine(
                ProjectionMode::P2D, 
                Float3(float(border._topLeft[0] + 2), float(border._topLeft[1]), 0.f), ColorB(0xffffffff),
                Float3(float(border._bottomRight[0] - 2), float(border._topLeft[1]), 0.f), ColorB(0xffffffff));
        }

            // Next should be a list of subdirectories to filter through
        {
            unsigned directoryIndex = 0;
            for (auto i=_pimpl->_subDirectories->_directories.cbegin(); i!=_pimpl->_subDirectories->_directories.cend(); ++i, ++directoryIndex) {
                char utf8Filename[MaxPath], baseName[MaxPath];
                ucs2_2_utf8(AsPointer(i->_filename.cbegin()), i->_filename.size(), (utf8*)utf8Filename, dimof(utf8Filename));
                XlBasename(baseName, dimof(baseName), utf8Filename);
                unsigned textWidth = 20 + (unsigned)StringWidth(*GetDefaultFont(), MakeStringSection(baseName));
                auto directoryRect = toolBoxLayout.Allocate(Coord2(textWidth, textHeight));
                DrawButtonBasic(
                    &context, directoryRect, baseName, 
                    FormatButton(interfaceState, Id_Directories+directoryIndex, buttonNormalState, buttonMouseOverState, buttonPressedState));
                interactables.Register(Interactables::Widget(directoryRect, Id_Directories+directoryIndex));
            }

            const char back[] = "<up>";
            unsigned textWidth = 20 + (unsigned)StringWidth(*GetDefaultFont(), MakeStringSection(back));
            auto directoryRect = toolBoxLayout.Allocate(Coord2(textWidth, textHeight));
            DrawButtonBasic(
                &context, directoryRect, back, 
                FormatButton(interfaceState, Id_BackDirectory, buttonNormalState, buttonMouseOverState, buttonPressedState));
            interactables.Register(Interactables::Widget(directoryRect, Id_BackDirectory));
        }

        {
            auto border = toolBoxLayout.AllocateFullWidth(2); // small border to reset current line
            context.DrawLine(
                ProjectionMode::P2D, 
                Float3(float(border._topLeft[0] + 2), float(border._topLeft[1]), 0.f), ColorB(0xffffffff),
                Float3(float(border._bottomRight[0] - 2), float(border._topLeft[1]), 0.f), ColorB(0xffffffff));
        }

            // Finally draw the models in this directory

        context.ReleaseState();        // (drawing directly to the device context -- so we must get the overlay context to release the state)

        std::vector<std::pair<std::string, Rect>> labels;

        Layout browserLayout(toolBoxLayout.AllocateFullHeightFraction(1.f));
        interactables.Register(Interactables::Widget(browserLayout.GetMaximumSize(), Id_MainSurface));
        
        unsigned itemsPerRow = (browserLayout.GetMaximumSize().Width() - 2 * browserLayout._paddingInternalBorder + browserLayout._paddingBetweenAllocations) / (_pimpl->_itemDimensions + browserLayout._paddingBetweenAllocations);
        unsigned rowsRequired = unsigned((_pimpl->_modelFiles->_files.size() + itemsPerRow - 1) / itemsPerRow);
        unsigned surfaceMaxSize = rowsRequired * (_pimpl->_itemDimensions + browserLayout._paddingBetweenAllocations);

        auto scrollBarRect = browserLayout.GetMaximumSize();
        const unsigned thumbWidth = 32;
        scrollBarRect._topLeft[0] = std::max(scrollBarRect._topLeft[0], scrollBarRect._bottomRight[0] - Coord(thumbWidth));
        ScrollBar::Coordinates scrollCoordinates(scrollBarRect, 0.f, float(surfaceMaxSize), float(browserLayout.GetMaximumSize().Height()));
        unsigned itemScrollOffset = unsigned(_mainScrollBar.CalculateCurrentOffset(scrollCoordinates));

#if 0   // platformtemp
        auto devContext = RenderCore::Metal::DeviceContext::Get(*context.GetDeviceContext());
        SceneEngine::SavedTargets oldTargets(*devContext.get());

        

            // let's render each model file to an off-screen buffer, and then copy the results to the main output
        for (auto i=_pimpl->_modelFiles->_files.cbegin(); i!=_pimpl->_modelFiles->_files.cend(); ++i) {

                // filter out some items based on the filename
            if (!Filter(i->_filename)) {
                continue;
            }

                // Make sure have ModelScaffold, MaterialScaffold and ModelRenderer for this object
            auto outputRect = browserLayout.Allocate(Coord2(_pimpl->_itemDimensions, _pimpl->_itemDimensions));
            if (outputRect._bottomRight[1] < Coord(browserLayout.GetMaximumSize()._topLeft[1] + browserLayout._paddingInternalBorder + itemScrollOffset))
                continue;
            outputRect._topLeft[1] -= itemScrollOffset;
            outputRect._bottomRight[1] -= itemScrollOffset;
            if (!outputRect.Width() || !outputRect.Height() || outputRect._topLeft[1] > toolBoxLayout.GetMaximumSize()._bottomRight[1])
                break;

            TRY 
            {

                utf8 utf8Filename[MaxPath];
                ucs2_2_utf8(AsPointer(i->_filename.cbegin()), i->_filename.size(), utf8Filename, dimof(utf8Filename));
                auto srv = GetSRV(*context.GetDeviceContext(), i->_filename);

                    // return to the main render target viewport & viewport -- and copy the offscreen image we've just rendered
                oldTargets.ResetToOldTargets(*devContext.get());
                Copy2DTexture(devContext.get(), *srv.first, 
                    Float2(float(outputRect._topLeft[0]), float(outputRect._topLeft[1])), 
                    Float2(float(outputRect._bottomRight[0]), float(outputRect._bottomRight[1])),
                    float(browserLayout.GetMaximumSize()._topLeft[1]), float(browserLayout.GetMaximumSize()._bottomRight[1]));

                interactables.Register(Interactables::Widget(outputRect, srv.second));

                char baseName[MaxPath];
                XlBasename(baseName, dimof(baseName), (const char*)utf8Filename);
                labels.push_back(std::make_pair(std::string(baseName), outputRect));

            } CATCH(const ::Assets::Exceptions::InvalidAsset& e) {
                labels.push_back(std::make_pair(std::string("Invalid res: ") + e.Initializer(), outputRect));
            } CATCH(const ::Assets::Exceptions::PendingAsset&) {
                labels.push_back(std::make_pair(std::string("Pending!"), outputRect));
            } CATCH(...) {
                labels.push_back(std::make_pair(std::string("Unknown exception"), outputRect));
            } CATCH_END

        }
#endif

        context.CaptureState();

        for (auto i=labels.cbegin(); i!=labels.cend(); ++i) {
            auto outputRect = i->second;
            OutlineRectangle(&context, Rect(outputRect._topLeft + Coord2(2,2), outputRect._bottomRight - Coord2(1,1)), 0.f, backgroundOutlineColour);
            Rect labelRect(Coord2(outputRect._topLeft[0], outputRect._bottomRight[1]-20), outputRect._bottomRight);

            context.DrawText(
                std::make_tuple(Float3(float(labelRect._topLeft[0]), float(labelRect._topLeft[1]), 0.f), Float3(float(labelRect._bottomRight[0]), float(labelRect._bottomRight[1]), 0.f)),
				GetDefaultFont(), TextStyle{}, ColorB(0xffffffff), TextAlignment::Center, i->first.c_str());
        }

            // draw the scroll bar over the top on the right size
        if (!scrollCoordinates.Collapse()) {
            auto thumbRect = scrollCoordinates.Thumb(float(itemScrollOffset));
            OutlineRectangle(&context, thumbRect, 0.f, backgroundOutlineColour);
        }
    }

    static const std::string Slashes("/\\");

    bool    SharedBrowser::ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputContext& inputContext, const PlatformRig::InputSnapshot& input)
    {
        if (input._wheelDelta && interfaceState.HasMouseOver(Id_MainSurface)) {
            _mainScrollBar.ProcessDelta(float(-input._wheelDelta));
            return true;
        }

        unsigned subDirCount = unsigned(_pimpl->_subDirectories ? _pimpl->_subDirectories->_directories.size() : 0);
        if (input.IsPress_LButton() || input.IsRelease_LButton()) {

                //  look through the entire mouse over stack to look for 
                //  buttons that are relevant to us
            auto stack = interfaceState.GetMouseOverStack();
            for (auto i=stack.cbegin(); i!=stack.cend(); ++i) {
                std::string newDirectory;
                bool consume = false;
                if (i->_id >= Id_Directories && i->_id < (Id_Directories + subDirCount)) {
                        // change directory to one of the subdirectories
                    if (input.IsRelease_LButton()) {
                        auto newDir = _pimpl->_subDirectories->_directories[unsigned(i->_id - Id_Directories)]._filename;
                        utf8 buffer[MaxPath];
                        ucs2_2_utf8(AsPointer(newDir.cbegin()), newDir.size(), buffer, dimof(buffer));
                        newDirectory = (const char*)buffer;
                    }
                    consume = true;
                } else if (i->_id == Id_BackDirectory) {
                        // this is "<up>... we need to go up one directory
                    if (input.IsRelease_LButton()) {
                        auto lastSlash = _pimpl->_currentDirectory.find_last_of(Slashes);
                        while (_pimpl->_currentDirectory.find_last_not_of(Slashes, lastSlash) == std::string::npos) {
                            if (lastSlash == 0 || lastSlash == std::string::npos) break;
                            lastSlash = _pimpl->_currentDirectory.find_last_of(Slashes, lastSlash-1);
                        }

                        if (lastSlash != std::string::npos) {
                            newDirectory = _pimpl->_currentDirectory.substr(0, lastSlash);
                        }
                    }
                    consume = true;
                }

                if (!newDirectory.empty()) {
                    _pimpl->_currentDirectory = newDirectory;
                    _pimpl->_subDirectories.reset();
                    _pimpl->_modelFiles.reset();
                }
                if (consume)
                    return true;
            }

        }


        return false;
    }

    SharedBrowser::SharedBrowser(const char baseDirectory[], const std::string& headerName, unsigned itemDimensions, const std::string& fileFilter)
        : _mainScrollBar(Id_MainScroller)
    {
        auto pimpl = std::make_unique<Pimpl>();
        pimpl->_baseDirectory = pimpl->_currentDirectory = baseDirectory;
        pimpl->_headingFont = GetX2Font("Raleway", 20);
        pimpl->_itemDimensions = itemDimensions;
        pimpl->_fileFilter = fileFilter;
        pimpl->_headerName = headerName;
        _pimpl = std::move(pimpl);
    }
    
    SharedBrowser::~SharedBrowser()
    {}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    using FixedFunctionModel::ModelCache;
	using FixedFunctionModel::ModelCacheModel;

    class ModelBrowser::Pimpl
    {
    public:
        RenderCore::Metal::RenderTargetView     _rtv;
        RenderCore::Metal::DepthStencilView     _dsv;
        RenderCore::Metal::ShaderResourceView   _srv;

        std::unique_ptr<ModelCache> _cache;
        std::vector<std::pair<uint64, std::string>> _modelNames;
    };

    static void RenderModel(
        RenderCore::IThreadContext& context, 
        ModelCacheModel& model)
    {
            // Render the given model. Create a minimal version of the full rendering process:
            //      We need to create a LightingParserContext, and ISceneParser as well.
            //      Cameras and lights should be arranged to suit the bounding box given. Let's use 
            //      orthogonal projection to make sure the object is positioned within the output viewport well.
		SceneEngine::SceneTechniqueDesc qualitySettings{};
        auto& metalContext = *RenderCore::Metal::DeviceContext::Get(context);
		(void)metalContext;

		assert(0); // broken in lighting parser refactoring. Needs a bit of restructing in the way ToolsRig::CreateModelScene works
				// with SceneEngine::IScene & SceneEngine::ILightingStateDelegate

        /*auto sceneParser = ToolsRig::CreateModelScene(model);
        Techniques::TechniqueContext techniqueContext;
		Techniques::ParsingContext parsingContext(techniqueContext);
        SceneEngine::LightingParser_ExecuteScene(
            context, parsingContext, *sceneParser.get(), 
            sceneParser->GetCameraDesc(), qualitySettings);*/
    }

    static const unsigned ModelBrowserItemDimensions = 196;

    std::pair<const RenderCore::Metal::ShaderResourceView*, uint64> ModelBrowser::GetSRV(
        RenderCore::IThreadContext& context, 
        const std::basic_string<ucs2>& filename)
    {
        auto metalContext = RenderCore::Metal::DeviceContext::Get(context);
            // draw this item into our offscreen target, and return the SRV
        
        utf8 utf8Filename[MaxPath];
        ucs2_2_utf8(AsPointer(filename.cbegin()), filename.size(), utf8Filename, dimof(utf8Filename));

        uint64 hashedName = Hash64((const char*)utf8Filename);
        auto ni = LowerBound(_pimpl->_modelNames, hashedName);
        if (ni == _pimpl->_modelNames.end() || ni->first != hashedName) {
            _pimpl->_modelNames.insert(ni, std::make_pair(hashedName, std::string((const char*)utf8Filename)));
        }

        auto model = _pimpl->_cache->GetModel((const ::Assets::ResChar*)utf8Filename, (const ::Assets::ResChar*)utf8Filename);

#if 0 //platformtemp
        SceneEngine::SavedTargets savedTargets(*metalContext.get());

            // draw this object to our off screen buffer
        const unsigned offscreenDims = ModelBrowserItemDimensions;
        metalContext->Bind(RenderCore::MakeResourceList(_pimpl->_rtv), &_pimpl->_dsv);
        metalContext->Bind(RenderCore::ViewportDesc(0, 0, float(offscreenDims), float(offscreenDims), 0.f, 1.f));
        metalContext->Clear(_pimpl->_rtv, {0.f, 0.f, 0.f, 1.f});
        metalContext->Clear(_pimpl->_dsv, RenderCore::Metal::DeviceContext::ClearFilter::Depth|RenderCore::Metal::DeviceContext::ClearFilter::Stencil, 1.f, 0);
        metalContext->Bind(Topology::TriangleList);
        RenderModel(context, model);

        savedTargets.ResetToOldTargets(*metalContext.get());
#endif

        return std::make_pair(&_pimpl->_srv, hashedName);   // note, here, the hashedName only considered the model name, not the material name
    }

    auto ModelBrowser::SpecialProcessInput(InterfaceState& interfaceState, const PlatformRig::InputContext& inputContext, const PlatformRig::InputSnapshot& input) -> ProcessInputResult
    {
        if (SharedBrowser::ProcessInput(interfaceState, inputContext, input)) {
            return true;
        }

        if (input.IsRelease_LButton()) {
            auto id = interfaceState.TopMostWidget()._id;
            auto model = LowerBound(_pimpl->_modelNames, id);
            if (model != _pimpl->_modelNames.end() && model->first == id) {
                return ProcessInputResult(true, model->second);
            }
        }

        return false;
    }

    bool ModelBrowser::Filter(const std::basic_string<ucs2>&)
    {
        return true;
    }

    Coord2  ModelBrowser::GetPreviewSize() const
    {
        return Coord2(ModelBrowserItemDimensions, ModelBrowserItemDimensions);
    }

    ModelBrowser::ModelBrowser(const char baseDirectory[])
    : SharedBrowser(baseDirectory, "Model Browser", ModelBrowserItemDimensions, "*.dae")
    {
        auto pimpl = std::make_unique<Pimpl>();

        const unsigned offscreenDims = ModelBrowserItemDimensions;
        auto& device = RenderCore::Assets::Services::GetDevice();
        using namespace RenderCore;
        ResourceDesc offscreenDesc;
        offscreenDesc._type = ResourceDesc::Type::Texture;
        offscreenDesc._bindFlags = BindFlag::RenderTarget | BindFlag::ShaderResource;
        offscreenDesc._cpuAccess = 0;
        offscreenDesc._gpuAccess = GPUAccess::Read | GPUAccess::Write;
        offscreenDesc._allocationRules = 0;
        offscreenDesc._textureDesc = TextureDesc::Plain2D(offscreenDims, offscreenDims, RenderCore::Format::R8G8B8A8_UNORM);
        auto offscreenResource = device.CreateResource(offscreenDesc);
        offscreenDesc._bindFlags = BindFlag::DepthStencil;
        offscreenDesc._textureDesc = TextureDesc::Plain2D(offscreenDims, offscreenDims, RenderCore::Format::D24_UNORM_S8_UINT);
        auto depthResource = device.CreateResource(offscreenDesc);
        pimpl->_rtv = RenderCore::Metal::RenderTargetView(offscreenResource);
        pimpl->_dsv = RenderCore::Metal::DepthStencilView(depthResource);
        pimpl->_srv = RenderCore::Metal::ShaderResourceView(offscreenResource);

        pimpl->_cache = std::make_unique<ModelCache>();

        _pimpl = std::move(pimpl);
    }

    ModelBrowser::~ModelBrowser() {}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class TextureBrowser::Pimpl
    {
    public:
        LRUCachePtr<::Assets::FuturePtr<RenderCore::Techniques::DeferredShaderResource>> _resources;
        Pimpl();
    };

    TextureBrowser::Pimpl::Pimpl() : _resources(256) {}

    TextureBrowser::TextureBrowser(const char baseDirectory[]) : SharedBrowser(baseDirectory, "Texture Browser", 196, "*.dds")
    {
        auto pimpl = std::make_unique<Pimpl>();
        _pimpl = std::move(pimpl);
    }

    TextureBrowser::~TextureBrowser() {}

    std::pair<const RenderCore::Metal::ShaderResourceView*, uint64> TextureBrowser::GetSRV(RenderCore::IThreadContext& devContext, const std::basic_string<ucs2>& filename)
    {
        uint64 hashedName = Hash64(AsPointer(filename.cbegin()), AsPointer(filename.cend()));
        auto res = _pimpl->_resources.Get(hashedName);
        if (!res) {
            utf8 utf8Filename[MaxPath];
            ucs2_2_utf8(AsPointer(filename.cbegin()), filename.size(), utf8Filename, dimof(utf8Filename));
            res = ::Assets::MakeAsset<RenderCore::Techniques::DeferredShaderResource>((const char*)utf8Filename);
            _pimpl->_resources.Insert(hashedName, res);
        }

		auto a = res->TryActualize();
        return std::make_pair(a ? &(*a)->GetShaderResource() : nullptr, hashedName);
    }

    bool TextureBrowser::Filter(const std::basic_string<ucs2>& filename)
    {
            // we want to exclude items that have "_sp", "_spec" or "_ddn"
        if (filename.find((ucs2*)u"_sp") != std::string::npos) { return false; }
        if (filename.find((ucs2*)u"_spec") != std::string::npos) { return false; }
        if (filename.find((ucs2*)u"_ddn") != std::string::npos) { return false; }
        return true;
    }

}