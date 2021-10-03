// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PlacementsManipulators.h"
#include "IManipulator.h"
#include "ManipulatorsUtil.h"
#include "ManipulatorsRender.h"
#include "VisualisationUtils.h"		// (for AsCameraDesc)

#include "../../SceneEngine/PlacementsManager.h"
//#include "../../SceneEngine/Terrain.h"
#include "../../SceneEngine/IntersectionTest.h"

#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../RenderOverlays/IOverlayContext.h"
#include "../../RenderOverlays/OverlayContext.h"
#include "../../RenderOverlays/Overlays/Browser.h"
//#include "../../RenderOverlays/OverlayUtils.h"
#include "../../RenderCore/IThreadContext.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/Techniques/DeferredShaderResource.h"

#include "../../Assets/Assets.h"

#include "../../Utility/StringFormat.h"
#include "../../Utility/Conversion.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Utility/StringUtils.h"
#include "../../Math/Transformations.h"
#include "../../Math/Geometry.h"
#include <iomanip>
#include <random>
#include <chrono>

namespace Overlays { class ModelBrowser; }

namespace ToolsRig
{
    using namespace RenderOverlays::DebuggingDisplay;

    IPlacementManipulatorSettings::~IPlacementManipulatorSettings() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    class PlacementsWidgets : public IWidget, public IPlacementManipulatorSettings
    {
    public:
        void    Render(         RenderOverlays::IOverlayContext& context, Layout& layout, 
                                Interactables& interactables, InterfaceState& interfaceState);
        void    RenderToScene(  RenderCore::IThreadContext& context, 
                                RenderCore::Techniques::ParsingContext& parserContext,
                                RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAccelerators);
        ProcessInputResult    ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputSnapshot& input);

        PlacementsWidgets(
            const std::shared_ptr<SceneEngine::PlacementsEditor>& editor, 
            const std::shared_ptr<SceneEngine::PlacementsRenderer>& renderer, 
            const std::shared_ptr<SceneEngine::IIntersectionScene>& intersectionTestScene,
			const std::shared_ptr<VisCameraSettings>& camera,
			const std::shared_ptr<RenderCore::Techniques::DrawingApparatus>& drawingApparatus);
        ~PlacementsWidgets();

    private:
        typedef RenderOverlays::DebuggingDisplay::IWidget ModelBrowser;

        std::shared_ptr<ModelBrowser>       _browser;
        std::shared_ptr<VisCameraSettings> _camera;
		std::shared_ptr<RenderCore::Techniques::DrawingApparatus> _drawingApparatus;
        std::shared_ptr<SceneEngine::IIntersectionScene> _intersectionTestScene;
        std::shared_ptr<SceneEngine::PlacementsEditor> _editor;
        bool            _browserActive;
        std::string     _selectedModel;
        std::string     _selectedMaterial;

        std::vector<std::unique_ptr<IManipulator>> _manipulators;
        unsigned        _activeManipulatorIndex;
        bool            _drawSelectedModel;

            // "IPlacementManipulatorSettings" interface
        virtual std::string GetSelectedModel() const;
        virtual std::string GetSelectedMaterial() const;
        virtual void EnableSelectedModelDisplay(bool newState);
        virtual void SelectModel(const char newModelName[], const char materialName[]);
        virtual void SwitchToMode(Mode::Enum newMode);
    };

///////////////////////////////////////////////////////////////////////////////////////////////////

    class SelectAndEdit : public IManipulator
    {
    public:
        bool OnInputEvent(
            const PlatformRig::InputSnapshot& evnt,
            const SceneEngine::IntersectionTestContext& hitTestContext,
            const SceneEngine::IIntersectionScene* hitTestScene);
        void Render(
            RenderCore::IThreadContext& context,
            RenderCore::Techniques::ParsingContext& parserContext,
            RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAccelerators);

        const char* GetName() const;
        std::string GetStatusText() const;

        IteratorRange<const FloatParameter*>  GetFloatParameters() const;
        IteratorRange<const BoolParameter*>   GetBoolParameters() const;
		IteratorRange<const IntParameter*>   GetIntParameters() const { return {}; }
        void SetActivationState(bool);

        SelectAndEdit(
            IPlacementManipulatorSettings* manInterface,
            std::shared_ptr<SceneEngine::PlacementsEditor> editor,
            std::shared_ptr<SceneEngine::PlacementsRenderer> renderer);
        ~SelectAndEdit();

    protected:
        std::shared_ptr<SceneEngine::PlacementsEditor> _editor;
        std::shared_ptr<SceneEngine::PlacementsRenderer> _renderer;
                  
        class SubOperation
        {
        public:
            enum Type { None, Translate, Scale, Rotate, MoveAcrossTerrainSurface };
            enum Axis { NoAxis, X, Y, Z };

            Type    _type;
            Float3  _parameter;
            Axis    _axisRestriction;
            Coord2  _cursorStart;
            SceneEngine::IntersectionTestResult _anchorTerrainIntersection;
            char    _typeInBuffer[4];

            SubOperation() : _type(None), _parameter(0.f, 0.f, 0.f), _axisRestriction(NoAxis), _cursorStart(0, 0) { _typeInBuffer[0] = '\0'; }
        };
        SubOperation _activeSubop;

        std::shared_ptr<SceneEngine::PlacementsEditor::ITransaction> _transaction;
        IPlacementManipulatorSettings* _manInterface;
        Float3 _anchorPoint;

        SceneEngine::PlacementsEditor::ObjTransDef TransformObject(
            const SceneEngine::IIntersectionScene& hitTestScene,
            const SceneEngine::PlacementsEditor::ObjTransDef& inputObj);
    };

    SceneEngine::PlacementsEditor::ObjTransDef SelectAndEdit::TransformObject(
        const SceneEngine::IIntersectionScene& hitTestScene,
        const SceneEngine::PlacementsEditor::ObjTransDef& inputObj)
    {
        Float4x4 transform;
        if (_activeSubop._type == SubOperation::Rotate) {
            transform = AsFloat4x4(Float3(-_anchorPoint));

            if (XlAbs(_activeSubop._parameter[0]) > 0.f) {
                Combine_IntoLHS(transform, RotationX(_activeSubop._parameter[0]));
            }

            if (XlAbs(_activeSubop._parameter[1]) > 0.f) {
                Combine_IntoLHS(transform, RotationY(_activeSubop._parameter[1]));
            }

            if (XlAbs(_activeSubop._parameter[2]) > 0.f) {
                Combine_IntoLHS(transform, RotationZ(_activeSubop._parameter[2]));
            }

            Combine_IntoLHS(transform, _anchorPoint);
        } else if (_activeSubop._type == SubOperation::Scale) {
            transform = AsFloat4x4(Float3(-_anchorPoint));
            Combine_IntoLHS(transform, ArbitraryScale(_activeSubop._parameter));
            Combine_IntoLHS(transform, _anchorPoint);
        } else if (_activeSubop._type == SubOperation::Translate) {
            transform = AsFloat4x4(_activeSubop._parameter);
        } else if (_activeSubop._type == SubOperation::MoveAcrossTerrainSurface) {
                //  move across terrain surface is a little different... 
                //  we have a 2d translation in XY. But then the Z values should be calculated
                //  from the terrain height.
            Float2 finalXY = Truncate(ExtractTranslation(inputObj._localToWorld)) + Truncate(_activeSubop._parameter);
            float terrainHeight = 0.f;
            auto terrainHeightQuery = SceneEngine::Stubs::GetTerrainHeight(hitTestScene, finalXY);
            if (terrainHeightQuery.has_value())
                terrainHeight = terrainHeightQuery.value();
            
            transform = AsFloat4x4(Float3(-ExtractTranslation(inputObj._localToWorld) + Expand(finalXY, terrainHeight)));
        } else {
            return inputObj;
        }

        auto res = inputObj;
        res._localToWorld = AsFloat3x4(Combine(res._localToWorld, transform));
        return res;
    }

    static void AppendStr(char buffer[], size_t bufferMaxCount, char newChar)
    {
        auto len = std::strlen(buffer);
        if (len+2 <= bufferMaxCount) {
            buffer[len] = newChar;
            buffer[len+1] = '\0';
        }
    }

    bool SelectAndEdit::OnInputEvent(
        const PlatformRig::InputSnapshot& evnt,
        const SceneEngine::IntersectionTestContext& hitTestContext,
        const SceneEngine::IIntersectionScene* hitTestScene)
    {
		if (!hitTestScene) return false;

        bool consume = false;
        if (_transaction) {
            static const auto keyG = PlatformRig::KeyId_Make("g");
            static const auto keyS = PlatformRig::KeyId_Make("s");
            static const auto keyR = PlatformRig::KeyId_Make("r");
            static const auto keyM = PlatformRig::KeyId_Make("m");
            static const auto keyEscape = PlatformRig::KeyId_Make("escape");

            static const auto keyX = PlatformRig::KeyId_Make("x");
            static const auto keyY = PlatformRig::KeyId_Make("y");
            static const auto keyZ = PlatformRig::KeyId_Make("z");

            bool updateState = evnt._mouseDelta[0] || evnt._mouseDelta[1];

            SubOperation::Type newSubOp = (SubOperation::Type)~unsigned(0);
            if (evnt.IsPress(keyG))         { newSubOp = SubOperation::Translate; _activeSubop._typeInBuffer[0] = '\0'; updateState = true; consume = true; }
            if (evnt.IsPress(keyS))         { newSubOp = SubOperation::Scale; _activeSubop._typeInBuffer[0] = '\0'; updateState = true; consume = true; }
            if (evnt.IsPress(keyR))         { newSubOp = SubOperation::Rotate; _activeSubop._typeInBuffer[0] = '\0'; updateState = true; consume = true; }
            if (evnt.IsPress(keyM))         { newSubOp = SubOperation::MoveAcrossTerrainSurface; _activeSubop._typeInBuffer[0] = '\0'; updateState = true; consume = true; }
            if (evnt.IsPress(keyEscape))    { newSubOp = SubOperation::None; _activeSubop._typeInBuffer[0] = '\0'; consume = true; }

            if (newSubOp != _activeSubop._type && unsigned(newSubOp) != ~unsigned(0x0)) {
                    //  we have to "restart" the transaction. This returns everything
                    //  to it's original place
                _transaction->UndoAndRestart();
                _activeSubop = SubOperation();
                _activeSubop._type = newSubOp;
                _activeSubop._axisRestriction = SubOperation::NoAxis;
                _activeSubop._cursorStart = evnt._mousePosition;

                if (newSubOp == SubOperation::MoveAcrossTerrainSurface) {
                    _activeSubop._anchorTerrainIntersection = hitTestScene->FirstRayIntersection(
                        hitTestContext,
                        hitTestContext.CalculateWorldSpaceRay(evnt._mousePosition), SceneEngine::IntersectionTestResult::Type::Terrain);
                }
            }

            if (_activeSubop._type != SubOperation::None) {
                Float3 oldParameter = _activeSubop._parameter;

                if (evnt.IsPress(keyX)) { _activeSubop._axisRestriction = SubOperation::X; _activeSubop._parameter = Float3(0.f, 0.f, 0.f); _activeSubop._typeInBuffer[0] = '\0'; updateState = true; consume = true; }
                if (evnt.IsPress(keyY)) { _activeSubop._axisRestriction = SubOperation::Y; _activeSubop._parameter = Float3(0.f, 0.f, 0.f); _activeSubop._typeInBuffer[0] = '\0'; updateState = true; consume = true; }
                if (evnt.IsPress(keyZ)) { _activeSubop._axisRestriction = SubOperation::Z; _activeSubop._parameter = Float3(0.f, 0.f, 0.f); _activeSubop._typeInBuffer[0] = '\0'; updateState = true; consume = true; }

                    // allow some characters to enter into the "type in buffer"
                    //      digits & '.' & '-'
                if ((evnt._pressedChar >= (ucs2)'0' && evnt._pressedChar <= (ucs2)'9') || evnt._pressedChar == (ucs2)'.' || evnt._pressedChar == (ucs2)'-') {
                    AppendStr(_activeSubop._typeInBuffer, dimof(_activeSubop._typeInBuffer), (char)evnt._pressedChar);
                    consume = true;
                    updateState = true;
                }

                if (updateState) {
                        //  we always perform a manipulator's action in response to a mouse movement.
                    if (_activeSubop._type == SubOperation::Rotate) {

                            //  rotate by checking the angle of the mouse cursor relative to the 
                            //  anchor point (in screen space)
                        auto ssAnchor = hitTestContext.ProjectToScreenSpace(_anchorPoint);
                        float ssAngle1 = XlATan2(evnt._mousePosition[1] - ssAnchor[1], evnt._mousePosition[0] - ssAnchor[0]);
                        float ssAngle0 = XlATan2(_activeSubop._cursorStart[1] - ssAnchor[1], _activeSubop._cursorStart[0] - ssAnchor[0]);
                        float value = ssAngle0 - ssAngle1;

                        if (_activeSubop._typeInBuffer[0]) {
                            value = XlAtoF32(_activeSubop._typeInBuffer) * gPI / 180.f;
                        }

                        unsigned axisIndex = 2;
                        switch (_activeSubop._axisRestriction) {
                        case SubOperation::X: axisIndex = 0; break;
                        case SubOperation::Y: axisIndex = 1; break;
                        case SubOperation::NoAxis:
                        case SubOperation::Z: axisIndex = 2; break;
                        }

                        _activeSubop._parameter = Float3(0.f, 0.f, 0.f);
                        _activeSubop._parameter[axisIndex] = value;

                    } else if (_activeSubop._type == SubOperation::Scale) {

                            //  Scale based on the distance (in screen space) between the cursor
                            //  and the anchor point, and compare that to the distance when we
                            //  first started this operation

                        auto ssAnchor = hitTestContext.ProjectToScreenSpace(_anchorPoint);
                        float ssDist1 = Magnitude(evnt._mousePosition - ssAnchor);
                        float ssDist0 = Magnitude(_activeSubop._cursorStart - ssAnchor);
                        float scaleFactor = 1.f;
                        if (ssDist0 > 0.f) {
                            scaleFactor = ssDist1 / ssDist0;
                        }

                        if (_activeSubop._typeInBuffer[0]) {
                            scaleFactor = XlAtoF32(_activeSubop._typeInBuffer);
                        }

                        switch (_activeSubop._axisRestriction) {
                        case SubOperation::X: _activeSubop._parameter = Float3(scaleFactor, 0.f, 0.f); break;
                        case SubOperation::Y: _activeSubop._parameter = Float3(0.f, scaleFactor, 0.f); break;
                        case SubOperation::Z: _activeSubop._parameter = Float3(0.f, 0.f, scaleFactor); break;
                        case SubOperation::NoAxis: _activeSubop._parameter = Float3(scaleFactor, scaleFactor, scaleFactor); break;
                        }

                    } else if (_activeSubop._type == SubOperation::Translate) {

                            //  We always translate across a 2d plane. So we need to define a plane 
                            //  based on the camera position and the anchor position.
                            //  We will calculate an intersection between a world space ray under the
                            //  cursor and that plane. That point (in 3d) will be the basis of the 
                            //  translation we apply.
                            //
                            //  The current "up" translation axis should lie flat on the plane. We
                            //  also want the camera "right" to lie close to the plane.

                        auto currentCamera = hitTestContext._cameraDesc;
                        Float3 upAxis = ExtractUp_Cam(currentCamera._cameraToWorld);
                        Float3 rightAxis = ExtractRight_Cam(currentCamera._cameraToWorld);
                        assert(Equivalent(MagnitudeSquared(upAxis), 1.f, 1e-6f));
                        assert(Equivalent(MagnitudeSquared(rightAxis), 1.f, 1e-6f));

                        switch (_activeSubop._axisRestriction) {
                        case SubOperation::X: upAxis = Float3(1.f, 0.f, 0.f); break;
                        case SubOperation::Y: upAxis = Float3(0.f, 1.f, 0.f); break;
                        case SubOperation::Z: upAxis = Float3(0.f, 0.f, 1.f); break;
                        default: break;
                        }

                        rightAxis = rightAxis - upAxis * Dot(upAxis, rightAxis);
                        if (MagnitudeSquared(rightAxis) < 1e-6f) {
                            rightAxis = ExtractUp_Cam(currentCamera._cameraToWorld);
                            rightAxis = rightAxis - upAxis * Dot(upAxis, rightAxis);
                        }

                        Float3 planeNormal = Cross(upAxis, rightAxis);
                        Float4 plane = Expand(planeNormal, -Dot(planeNormal, _anchorPoint));

                        auto initialRay = hitTestContext.CalculateWorldSpaceRay(_activeSubop._cursorStart);
                        float initialDst = RayVsPlane(initialRay.first, initialRay.second, plane);

                        auto newRay = hitTestContext.CalculateWorldSpaceRay(evnt._mousePosition);
                        float newDst = RayVsPlane(newRay.first, newRay.second, plane);
                        if (newDst >= 0.f && newDst <= 1.f && initialDst >= 0.f && initialDst <= 1.f) {
                            auto startPt = LinearInterpolate(initialRay.first, initialRay.second, initialDst);
                            auto intersectionPt = LinearInterpolate(newRay.first, newRay.second, newDst);

                            float transRight = Dot(intersectionPt - startPt, rightAxis);
                            float transUp = Dot(intersectionPt - startPt, upAxis);

                            if (_activeSubop._axisRestriction != SubOperation::NoAxis && _activeSubop._typeInBuffer[0]) {
                                transUp = XlAtoF32(_activeSubop._typeInBuffer);
                            }

                            switch (_activeSubop._axisRestriction) {
                            case SubOperation::X: _activeSubop._parameter = Float3(transUp, 0.f, 0.f); break;
                            case SubOperation::Y: _activeSubop._parameter = Float3(0.f, transUp, 0.f); break;
                            case SubOperation::Z: _activeSubop._parameter = Float3(0.f, 0.f, transUp); break;
                            case SubOperation::NoAxis:
                                _activeSubop._parameter = transRight * rightAxis + transUp * upAxis;
                                break;
                            }
                        }

                    } else if (_activeSubop._type == SubOperation::MoveAcrossTerrainSurface) {

                            //  We want to find an intersection point with the terrain, and then 
                            //  compare the XY coordinates of that to the anchor point

                        auto collision = hitTestScene->FirstRayIntersection(hitTestContext, hitTestContext.CalculateWorldSpaceRay(evnt._mousePosition), SceneEngine::IntersectionTestResult::Type::Terrain);
                        if (collision._type == SceneEngine::IntersectionTestResult::Type::Terrain
                            && _activeSubop._anchorTerrainIntersection._type == SceneEngine::IntersectionTestResult::Type::Terrain) {
                            _activeSubop._parameter = Float3(
                                collision._worldSpaceCollision[0] - _anchorPoint[0],
                                collision._worldSpaceCollision[1] - _anchorPoint[1],
                                0.f);
                        }

                    }
                }

                if (_activeSubop._parameter != oldParameter) {
                        // push these changes onto the transaction
                    unsigned count = _transaction->GetObjectCount();
                    for (unsigned c=0; c<count; ++c) {
                        auto& originalState = _transaction->GetObjectOriginalState(c);
                        auto newState = TransformObject(*hitTestScene, originalState);
                        _transaction->SetObject(c, newState);
                    }
                }
            }
        }
        

            //  On lbutton click, attempt to do hit detection
            //  select everything that intersects with the given ray
        if (evnt.IsRelease_LButton()) {

            if (_activeSubop._type != SubOperation::None) {

                if (_transaction) { 
                    _transaction->Commit();  
                    _transaction.reset();
                }
                _activeSubop = SubOperation();

            } else {

                auto worldSpaceRay = hitTestContext.CalculateWorldSpaceRay(evnt._mousePosition);
                
                SceneEngine::PlacementGUID firstHit(0,0);
                auto hitTestResult = hitTestScene->FirstRayIntersection(hitTestContext, worldSpaceRay);
                if (hitTestResult._type == SceneEngine::IntersectionTestResult::Type::Placement) {
                    firstHit = hitTestResult._objectGuid;
                }

                    // replace the currently active selection
                if (_transaction) {
                    _transaction->Commit();
                    _transaction.reset();
                }

                if (firstHit.first && firstHit.second) {
                    _transaction = _editor->Transaction_Begin(&firstHit, &firstHit + 1);
                
                        //  Reset the anchor point
                        //  There are a number of different possible ways we could calculate
                        //      the anchor point... But let's find the world space bounding box
                        //      that encloses all of the objects and get the centre of that box.
                    enum AnchorMethod { Centre, Origin };
                    const AnchorMethod anchorMethod = Origin;

                    _anchorPoint = Float3(0.f, 0.f, 0.f);
                    unsigned objCount = _transaction->GetObjectCount();
                    if (objCount) {
                        if (constant_expression<anchorMethod == Centre>::result()) {
                            Float3 totalMins(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()), totalMaxs(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());
                            for (unsigned c=0; c<objCount; ++c) {
                                auto obj = _transaction->GetObject(c);
                                auto localBoundingBox = _transaction->GetLocalBoundingBox(c);
                                auto worldSpaceBounding = TransformBoundingBox(obj._localToWorld, localBoundingBox);
                                totalMins[0] = std::min(worldSpaceBounding.first[0], totalMins[0]);
                                totalMins[1] = std::min(worldSpaceBounding.first[1], totalMins[1]);
                                totalMins[2] = std::min(worldSpaceBounding.first[2], totalMins[2]);
                                totalMaxs[0] = std::max(worldSpaceBounding.second[0], totalMaxs[0]);
                                totalMaxs[1] = std::max(worldSpaceBounding.second[1], totalMaxs[1]);
                                totalMaxs[2] = std::max(worldSpaceBounding.second[2], totalMaxs[2]);
                            }
                            _anchorPoint = LinearInterpolate(totalMins, totalMaxs, 0.5f);
                        } else if (constant_expression<anchorMethod == Origin>::result()) {
                            for (unsigned c=0; c<objCount; ++c) {
                                _anchorPoint += ExtractTranslation(_transaction->GetObject(c)._localToWorld);
                            }
                            _anchorPoint /= float(objCount);
                        }
                    }
                }
            }

            return true;
        }

        if (evnt.IsDblClk_LButton()) {

                //  We want to select the name model that we dbl-clked on.
                //  Actually, the model under the cursor is probably in
                //  the selection now. But let's do another ray test.
                //  Then, tell the manager to switch to placement mode

            if (_manInterface) {
                auto worldSpaceRay = hitTestContext.CalculateWorldSpaceRay(evnt._mousePosition);
                
                SceneEngine::PlacementGUID firstHit(0,0);
                auto hitTestResult = hitTestScene->FirstRayIntersection(hitTestContext, worldSpaceRay);
                if (hitTestResult._type == SceneEngine::IntersectionTestResult::Type::Placement) {
                    auto tempTrans = _editor->Transaction_Begin(&hitTestResult._objectGuid, &hitTestResult._objectGuid + 1);
                    if (tempTrans->GetObjectCount() == 1) {
                        _manInterface->SelectModel(tempTrans->GetObject(0)._model.c_str(), tempTrans->GetObject(0)._model.c_str());
                        _manInterface->SwitchToMode(IPlacementManipulatorSettings::Mode::PlaceSingle);
                    }
                }
            }

            return true;
        }

        if (evnt.IsPress(PlatformRig::KeyId_Make("delete"))) {
            if (_transaction) {
                auto count = _transaction->GetObjectCount();
                for (unsigned c=0; c<count; ++c) { _transaction->Delete(c); }
                _activeSubop = SubOperation();
            }
            return true;
        }

        return consume;
    }

    void SelectAndEdit::Render(
        RenderCore::IThreadContext& context,
        RenderCore::Techniques::ParsingContext& parserContext,
        RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAccelerators)
    {
        std::vector<std::pair<uint64_t, uint64_t>> activeSelection;

        if (_transaction) {
            activeSelection.reserve(_transaction->GetObjectCount());
            for (unsigned c=0; c<_transaction->GetObjectCount(); ++c) {
                activeSelection.push_back(_transaction->GetGuid(c));
            }
        }

        if (!activeSelection.empty()) {
                //  If we have some selection, we need to render it
                //  to an offscreen buffer, and we can perform some
                //  operation to highlight the objects in that buffer.
                //
                //  Note that we could get different results by doing
                //  this one placement at a time -- but we will most
                //  likely get the most efficient results by rendering
                //  all of objects that require highlights in one go.
            Placements_RenderHighlight(
                context, parserContext, pipelineAccelerators, *_renderer, _editor->GetCellSet(),
                AsPointer(activeSelection.begin()), AsPointer(activeSelection.end()));
        }
    }

    std::string SelectAndEdit::GetStatusText() const
    {
        if (_activeSubop._type == SubOperation::None) {
            return std::string();
        }

        StringMeld<512> meld;
        meld << std::fixed << std::setprecision(2);

        switch (_activeSubop._type)
        {
        case SubOperation::Translate:
            meld << "T: (" << _activeSubop._parameter[0] << ", " << _activeSubop._parameter[1] << "," << _activeSubop._parameter[1] << "). ";
            break;

        case SubOperation::Scale:
            meld << "S: (" << _activeSubop._parameter[0] << ", " << _activeSubop._parameter[1] << "," << _activeSubop._parameter[1] << "). ";
            break;

        case SubOperation::Rotate:
            switch (_activeSubop._axisRestriction) {
            case SubOperation::X: meld << "R: (" << _activeSubop._parameter[0] * 180.f / gPI << ")"; break;
            case SubOperation::Y: meld << "R: (" << _activeSubop._parameter[1] * 180.f / gPI << ")"; break;
            case SubOperation::NoAxis:
            case SubOperation::Z: meld << "R: (" << _activeSubop._parameter[2] * 180.f / gPI << ")"; break;
            }
            break;

        case SubOperation::MoveAcrossTerrainSurface:
            meld << "T <<terrain>>: (" << _activeSubop._parameter[0] << ", " << _activeSubop._parameter[1] << "). ";
            break;

        default:break;
        }

        switch (_activeSubop._axisRestriction) 
        {
        case SubOperation::NoAxis: break;
        case SubOperation::X: meld << " [ Global X ] "; break;
        case SubOperation::Y: meld << " [ Global Y ] "; break;
        case SubOperation::Z: meld << " [ Global Z ] "; break;
        }

        return std::string(meld);
    }

    const char* SelectAndEdit::GetName() const                                            { return "Select And Edit"; }
	auto SelectAndEdit::GetFloatParameters() const -> IteratorRange<const FloatParameter*>  { return {}; }
	auto SelectAndEdit::GetBoolParameters() const -> IteratorRange<const BoolParameter*>    { return {}; }
    void SelectAndEdit::SetActivationState(bool) 
    {
        if (_transaction) {
            _transaction->Cancel();
            _transaction.reset();
        }
        _activeSubop = SubOperation();
        _anchorPoint = Float3(0.f, 0.f, 0.f);
    }

    SelectAndEdit::SelectAndEdit(
        IPlacementManipulatorSettings* manInterface,
        std::shared_ptr<SceneEngine::PlacementsEditor> editor,
        std::shared_ptr<SceneEngine::PlacementsRenderer> renderer)
    {
        _manInterface = manInterface;
        _editor = editor;
        _renderer = renderer;
        _anchorPoint = Float3(0.f, 0.f, 0.f);
    }

    SelectAndEdit::~SelectAndEdit()
    {}

    ///////////////////////////////////////////////////////////////////////////////////////////////////

    class PlaceSingle : public IManipulator
    {
    public:
        bool OnInputEvent(
            const PlatformRig::InputSnapshot& evnt,
            const SceneEngine::IntersectionTestContext& hitTestContext,
            const SceneEngine::IIntersectionScene* hitTestScene);
        void Render(
            RenderCore::IThreadContext& context,
            RenderCore::Techniques::ParsingContext& parserContext,
            RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAccelerators);

        const char* GetName() const;
        IteratorRange<const FloatParameter*>  GetFloatParameters() const;
        IteratorRange<const BoolParameter*>   GetBoolParameters() const;
		IteratorRange<const IntParameter*>   GetIntParameters() const { return {}; }
        void SetActivationState(bool);
        std::string GetStatusText() const { return std::string(); }

        PlaceSingle(
            IPlacementManipulatorSettings* manInterface,
            std::shared_ptr<SceneEngine::PlacementsEditor> editor,
            std::shared_ptr<SceneEngine::PlacementsRenderer> renderer);
        ~PlaceSingle();

    protected:
        std::chrono::steady_clock::time_point   _placeTimeout;
        IPlacementManipulatorSettings*  _manInterface;
        std::shared_ptr<SceneEngine::PlacementsEditor> _editor;
        std::shared_ptr<SceneEngine::PlacementsRenderer> _renderer;
        unsigned                        _rendersSinceHitTest;

        bool        _doRandomRotation;
        float       _placementRotation;

        std::shared_ptr<SceneEngine::PlacementsEditor::ITransaction> _transaction;

        void MoveObject(
            const Float3& newLocation,
            const char modelName[], const char materialName[]);
    };

    static void Combine_IntoRHS_(RotationZ rotation, Float3x4& transform)
    {
        auto temp = AsFloat4x4(transform);
        Combine_IntoRHS(rotation, temp);
        transform = AsFloat3x4(temp);
    }

    void PlaceSingle::MoveObject(const Float3& newLocation, 
        const char modelName[], const char materialName[])
    {
        if (!_transaction) {
            _transaction = _editor->Transaction_Begin(nullptr, nullptr);
        }

        SceneEngine::PlacementsEditor::ObjTransDef newState(
            AsFloat3x4(newLocation), _manInterface->GetSelectedModel(), materialName, nullptr);
        if (_doRandomRotation) {
            Combine_IntoRHS_(RotationZ(_placementRotation), newState._localToWorld);
        }

        TRY {
            if (!_transaction->GetObjectCount()) {
                _placementRotation = rand() * 2.f * gPI / float(RAND_MAX);
                newState._localToWorld = AsFloat3x4(newLocation);
                if (_doRandomRotation) {
                    Combine_IntoRHS_(RotationZ(_placementRotation), newState._localToWorld);
                }

                _transaction->Create(newState);
                _placeTimeout = std::chrono::steady_clock::now();
            } else {
                _transaction->SetObject(0, newState);
            }
        } CATCH (...) {
        } CATCH_END
    }

    bool PlaceSingle::OnInputEvent(
        const PlatformRig::InputSnapshot& evnt,
        const SceneEngine::IntersectionTestContext& hitTestContext,
        const SceneEngine::IIntersectionScene* hitTestScene)
    {
        //  If we get a click on the terrain, then we should perform 
            //  whatever placement operation is required (eg, creating new placements)
        auto selectedModel = _manInterface->GetSelectedModel();
        if (selectedModel.empty()) {
            if (_transaction) {
                _transaction->Cancel();
                _transaction.reset();
            }
            return false;
        }

        if (_rendersSinceHitTest > 0 && hitTestScene) {
            _rendersSinceHitTest = 0;

            auto test = hitTestScene->FirstRayIntersection(hitTestContext, hitTestContext.CalculateWorldSpaceRay(evnt._mousePosition), SceneEngine::IntersectionTestResult::Type::Terrain);
            if (test._type == SceneEngine::IntersectionTestResult::Type::Terrain) {

                    //  This is a spawn event. We should add a new item of the selected model
                    //  at the point clicked.
                MoveObject(test._worldSpaceCollision, selectedModel.c_str(), "");
            }
        }

            // We add a small timeout 
        const auto safetyTimeout = std::chrono::milliseconds(200);
        if (evnt.IsRelease_LButton() && (_placeTimeout - std::chrono::steady_clock::now()) > safetyTimeout) {
            if (_transaction) {
                _transaction->Commit();
                _transaction.reset();
            }
        }

        if (evnt.IsRelease_RButton() || evnt.IsPress(PlatformRig::KeyId_Make("escape"))) {
            // cancel... tell the manager to change model
            if (_manInterface) {
                _manInterface->SwitchToMode(IPlacementManipulatorSettings::Mode::Select);
            }
        }

        return false;
    }

    void PlaceSingle::Render(
        RenderCore::IThreadContext& context,
        RenderCore::Techniques::ParsingContext& parserContext,
        RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAccelerators)
    {
        ++_rendersSinceHitTest;
        if (_transaction && _transaction->GetObjectCount()) {
            std::vector<SceneEngine::PlacementGUID> objects;
            objects.reserve(_transaction->GetObjectCount());
            for (unsigned c=0; c<_transaction->GetObjectCount(); ++c) {
                objects.push_back(_transaction->GetGuid(c));
            }

            Placements_RenderHighlight(
                context, parserContext, pipelineAccelerators, *_renderer, _editor->GetCellSet(),
                AsPointer(objects.begin()), AsPointer(objects.end()));
        }
    }

    const char* PlaceSingle::GetName() const                                            { return "Place single"; }
	auto PlaceSingle::GetFloatParameters() const -> IteratorRange<const FloatParameter*>  { return {}; }
    auto PlaceSingle::GetBoolParameters() const -> IteratorRange<const BoolParameter*>    
    {
        static BoolParameter parameters[] = 
        {
			BoolParameter{ManipulatorParameterOffset(&PlaceSingle::_doRandomRotation), 0, "RandomRotation"}
        };
        return MakeIteratorRange(parameters);
    }

    void PlaceSingle::SetActivationState(bool newState)
    {
        if (_transaction) {
            _transaction->Cancel();
            _transaction.reset();
        }
        if (_manInterface) {
            _manInterface->EnableSelectedModelDisplay(newState);
        }
        _placeTimeout = std::chrono::steady_clock::now();
    }

    PlaceSingle::PlaceSingle(
        IPlacementManipulatorSettings* manInterface,
        std::shared_ptr<SceneEngine::PlacementsEditor> editor,
        std::shared_ptr<SceneEngine::PlacementsRenderer> renderer)
    {
        _placeTimeout = std::chrono::steady_clock::time_point();
        _manInterface = manInterface;
        _editor = std::move(editor);
        _renderer = std::move(renderer);
        _rendersSinceHitTest = 0;
        _doRandomRotation = true;
        _placementRotation = 0.f;
    }
    
    PlaceSingle::~PlaceSingle() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    class ScatterPlacements : public IManipulator
    {
    public:
        bool OnInputEvent(
            const PlatformRig::InputSnapshot& evnt,
            const SceneEngine::IntersectionTestContext& hitTestContext,
            const SceneEngine::IIntersectionScene* hitTestScene);
        void Render(
            RenderCore::IThreadContext& context,
            RenderCore::Techniques::ParsingContext& parserContext,
            RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAccelerators);

        const char* GetName() const;
        IteratorRange<const FloatParameter*>  GetFloatParameters() const;
        IteratorRange<const BoolParameter*>   GetBoolParameters() const;
		IteratorRange<const IntParameter*>   GetIntParameters() const { return {}; }
        void SetActivationState(bool);
        std::string GetStatusText() const { return std::string(); }

        ScatterPlacements(
            IPlacementManipulatorSettings* manInterface,
            std::shared_ptr<SceneEngine::PlacementsEditor> editor);
        ~ScatterPlacements();

    protected:
        std::chrono::steady_clock::time_point   _spawnTimer;
        IPlacementManipulatorSettings*    _manInterface;
        std::shared_ptr<SceneEngine::PlacementsEditor> _editor;

        float _radius;
        float _density;

        Float3 _hoverPoint;
        bool _hasHoverPoint;

        void PerformScatter(
            const SceneEngine::IIntersectionScene& hitTestScene,
            const Float3& centre, const char modelName[], const char materialName[]);
    };

    bool ScatterPlacements::OnInputEvent(
        const PlatformRig::InputSnapshot& evnt,
        const SceneEngine::IntersectionTestContext& hitTestContext,
        const SceneEngine::IIntersectionScene* hitTestScene)
    {
            //  If we get a click on the terrain, then we should perform 
            //  whatever placement operation is required (eg, creating new placements)
            //
            //  However, we need to do terrain collisions every time (because we want to
            //  move the highlight/preview position

		if (!hitTestScene) return false;

        auto test = hitTestScene->FirstRayIntersection(hitTestContext, hitTestContext.CalculateWorldSpaceRay(evnt._mousePosition), SceneEngine::IntersectionTestResult::Type::Terrain);
        _hoverPoint = test._worldSpaceCollision;
        _hasHoverPoint = test._type == SceneEngine::IntersectionTestResult::Type::Terrain;

        const auto spawnTimeOut = std::chrono::milliseconds(200);
        auto now = std::chrono::steady_clock::now();
        if (evnt.IsHeld_LButton()) {
            if (test._type == SceneEngine::IntersectionTestResult::Type::Terrain) {
                auto selectedModel = _manInterface->GetSelectedModel();
                auto selectedMaterial = _manInterface->GetSelectedMaterial();
                if (now >= (_spawnTimer + spawnTimeOut) && !selectedModel.empty() && !selectedMaterial.empty()) {
                    PerformScatter(*hitTestScene, test._worldSpaceCollision, selectedModel.c_str(), selectedMaterial.c_str());
                    _spawnTimer = now;
                }
                return true;
            }
        } else { _spawnTimer = {}; }

        if (evnt._wheelDelta) {
            _radius = std::max(1.f, _radius + 3.f * evnt._wheelDelta / 120.f);
        }

        if (evnt.IsRelease_RButton() || evnt.IsPress(PlatformRig::KeyId_Make("escape"))) {
            // cancel... tell the manager to change model
            if (_manInterface) {
                _manInterface->SwitchToMode(IPlacementManipulatorSettings::Mode::Select);
            }
        }

        return false;
    }

    static float TriangleSignedArea(const Float2& pt1, const Float2& pt2, const Float2& pt3)
    {
        // reference:
        //  http://mathworld.wolfram.com/TriangleArea.html
        return .5f * (
            -pt2[0] * pt1[1] + pt3[0] * pt1[1] + pt1[0] * pt2[1]
            -pt3[0] * pt2[1] - pt1[0] * pt3[1] + pt2[0] * pt3[1]);
    }

    static bool PtInConvexPolygon(
        const unsigned* ptIndicesStart, const unsigned* ptIndicesEnd,
        const Float2 pts[], const Float2& pt)
    {
        unsigned posCount = 0, negCount = 0;
        for (auto* p=ptIndicesStart; (p+1)<ptIndicesEnd; ++p) {
            auto area = TriangleSignedArea(pts[*p], pts[*(p+1)], pt);
            if (area < 0.f) { ++negCount; } else { ++posCount; }
        }

        auto area = TriangleSignedArea(pts[*(ptIndicesEnd-1)], pts[*ptIndicesStart], pt);
        if (area < 0.f) { ++negCount; } else { ++posCount; }

            //  we're not making assumption about winding order. So 
            //  return true if every result is positive, or every result
            //  is negative; (in other words, one of the following must be zero)
        return posCount*negCount == 0;
    }

    static Float2 PtClosestToOrigin(const Float2& start, const Float2& end)
    {
        Float2 axis = end - start;
        float length = Magnitude(axis);
        Float2 dir = axis / length;
        float proj = Dot(-start, dir);
        return LinearInterpolate(start, end, Clamp(proj, 0.f, length));
    }

    /// <summary>Compare an axially aligned bounding box to a circle (in 2d, on the XY plane)</summary>
    /// This is a cheap alternative to box vs cylinder
    static bool AABBVsCircleInXY(
        float circleRadius, const std::pair<Float3, Float3>& boundingBox, const Float3x4& localToCircleSpace)
    {
        Float3 pts[] = 
        {
            Float3( boundingBox.first[0],  boundingBox.first[1],  boundingBox.first[2]),
            Float3(boundingBox.second[0],  boundingBox.first[1],  boundingBox.first[2]),
            Float3( boundingBox.first[0], boundingBox.second[1],  boundingBox.first[2]),
            Float3(boundingBox.second[0], boundingBox.second[1],  boundingBox.first[2]),
            Float3( boundingBox.first[0],  boundingBox.first[1], boundingBox.second[2]),
            Float3(boundingBox.second[0],  boundingBox.first[1], boundingBox.second[2]),
            Float3( boundingBox.first[0], boundingBox.second[1], boundingBox.second[2]),
            Float3(boundingBox.second[0], boundingBox.second[1], boundingBox.second[2])
        };

        Float2 testPts[dimof(pts)];
        for (unsigned c=0; c<dimof(pts); ++c) {
                // Z part is ignored completely
            testPts[c] = Truncate(TransformPoint(localToCircleSpace, pts[c]));
        }

        unsigned faces[6][4] = 
        {
            { 0, 1, 2, 3 },
            { 1, 5, 6, 2 },
            { 5, 4, 7, 6 },
            { 4, 0, 3, 7 },
            { 4, 5, 1, 0 },
            { 3, 2, 6, 7 },
        };
        
            //  We've made 6 rhomboids in 2D. We want to compare these to the circle
            //  at the origin, with the given radius.
            //  We need to find the point on the rhomboid that is closest to the origin.
            //  this will always lie on an edge (unless the origin is actually within the
            //  rhomboid). So we can just find the point on each edge is that closest to
            //  the origin, and see if that's within the radius.

        for (unsigned f=0; f<6; ++f) {
                //  first, is the origin within the rhumboid. Our pts are arranged in 
                //  winding order. So we can use a simple pt in convex polygon test.
            if (PtInConvexPolygon(faces[f], &faces[f][4], testPts, Float2(0.f, 0.f))) {
                return true; // pt is inside, so we have an interestion.
            }

            for (unsigned e=0; e<4; ++e) {
                auto edgeStart  = testPts[faces[f][e]];
                auto edgeEnd    = testPts[faces[f][(e+1)%4]];

                auto pt = PtClosestToOrigin(edgeStart, edgeEnd);
                if (MagnitudeSquared(pt) <= (circleRadius*circleRadius)) {
                    return true; // closest pt is within circle -- so return true
                }
            }
        }

        return false;
    }

    static Float2 Radial2Cart2D(float theta, float r)
    {
        auto t = XlSinCos(theta);
        return r * Float2(std::get<0>(t), std::get<1>(t));
    }

    static bool IsBlueNoiseGoodPoint(const std::vector<Float2>& existingPts, const Float2& testPt, float dRSq)
    {
        for (auto i=existingPts.begin(); i!=existingPts.end(); ++i) {
            if (MagnitudeSquared(testPt - *i) < dRSq) {
                return false;
            }
        }
        return true;
    }

	static void EraseRandomPoints(std::vector<Float2>& workingSet, size_t idealSize, std::mt19937& generator)
	{
		// Randomly erase items in the list until we are at the ideal size.
		// But, let's weight the random distribution so that objects near the center
		// of the the circle are more likely to be removed.
		if (workingSet.size() <= idealSize) return;

		std::vector<size_t> sortedIndices;
		sortedIndices.reserve(workingSet.size());
		for (size_t c=0; c<workingSet.size(); ++c)
			sortedIndices.push_back(c);
		
		std::sort(
			sortedIndices.begin(), sortedIndices.end(),
			[&workingSet](size_t lhs, size_t rhs)
			{
				return MagnitudeSquared(workingSet[lhs]) < MagnitudeSquared(workingSet[rhs]);
			});

		const float biasFactor = 3.f;
		std::vector<size_t> indicesToRemove;
		indicesToRemove.reserve(sortedIndices.size() - idealSize);
		while (sortedIndices.size() > idealSize) {
			float value = std::uniform_real_distribution<float>(0.f, 1.f)(generator);
			value = std::pow(value, biasFactor);
			size_t index = (size_t)(value * float(sortedIndices.size()-1));
			indicesToRemove.push_back(sortedIndices[index]);
			sortedIndices.erase(sortedIndices.begin() + index);
		}

		// now, finally remove those indices from "workingSet"
		std::sort(indicesToRemove.begin(), indicesToRemove.end());
		for (auto i=indicesToRemove.rbegin(); i!=indicesToRemove.rend(); ++i)
			workingSet.erase(workingSet.begin() + *i);
	}

    static void GenerateBlueNoisePlacements(std::vector<Float2>& workingSet, float radius, unsigned count, std::mt19937& generator)
    {
            //  Create new placements arranged in a equally spaced pattern
            //  around the circle.
            //  We're going to use a blue-noise pattern to calculate points
            //  in this circle. This method can be used for generate poisson
            //  disks. Here is the reference:
            //  http://www.cs.ubc.ca/~rbridson/docs/bridson-siggraph07-poissondisk.pdf

            //  We can use an expectation for the ratio of the inner circle radius to the
            //  outer radius to decide on the radius used for the poisson calculations.
            //
            //  From this page, we can see the "ideal" density values for the best circle
            //  packing results. Our method won't produce the ideal results, but so we
            //  need to use an expected density that is smaller.
            //  http://hydra.nat.uni-magdeburg.de/packing/cci/cci.html
        const float expectedDensity = 0.65f;

        // prevent "count" from getting too high, because these loops can end up very expensive
        count = std::min(count, 1000u);

        const float bigCircleArea = gPI * radius * radius;
        const float littleCircleArea = bigCircleArea * expectedDensity / float(count);
        const float littleCircleRadius = sqrt(littleCircleArea / gPI);
        const float dRSq = 4*littleCircleRadius*littleCircleRadius;

        const unsigned k = 30;
        workingSet.reserve(count);
        if (workingSet.empty())
            workingSet.push_back(
                Radial2Cart2D(
					std::uniform_real_distribution<float>(0.f, 2.f * gPI)(generator),
					(std::uniform_real_distribution<float>(.125f * radius, .25f * radius)(generator))));

            // erase random objects to reduce the number
		EraseRandomPoints(workingSet, count, generator);

        const unsigned iterationCount = count; // 2 * count - 1;
        for (unsigned c=0; c<iterationCount && workingSet.size() < count; ++c) {
            assert(!workingSet.empty());
            // unsigned index = rand() % unsigned(workingSet.size());
			auto index = std::uniform_int_distribution<size_t>(0, workingSet.size()-1)(generator);

                // look for a good random connector
            bool gotGoodPt = false;
            float startAngle = std::uniform_real_distribution<float>(0.f, 2.f * gPI)(generator);
            for (unsigned t=0; t<k; ++t) {
                Float2 pt = workingSet[index] + Radial2Cart2D(
                    startAngle + float(t) * (2.f * gPI / float(k)),
                    littleCircleRadius * (std::uniform_real_distribution<float>(2.f, 3.f)(generator)));

                if (MagnitudeSquared(pt) > radius * radius) {
                    continue;   // bad pt; outside of large radius. We need the centre to be within the large radius
                }

                    // note -- we can accelerate this test with 
                    //         a 2d lookup grid 
                if (IsBlueNoiseGoodPoint(workingSet, pt, dRSq)) {
                    gotGoodPt = true;
                    workingSet.push_back(pt);
                    break;
                }
            }

                // if we couldn't find a good connector, we have to erase the original pt 
            if (!gotGoodPt) {
                workingSet.erase(workingSet.begin() + index);

                    //  Note; there can be weird cases where the original point is remove
                    //  in these cases, we have to add back a new starter point. We can't
                    //  let the working state get empty
                if (workingSet.empty()) {
                    workingSet.push_back(
                        Radial2Cart2D(
							std::uniform_real_distribution<float>(0.f, 2.f * gPI)(generator),
							(std::uniform_real_distribution<float>(.125f * radius, .25f * radius)(generator))));
                }
            }
        }

            // if we didn't get enough, it's ok
            // we'll probably add more in the next iteration (since most
            // of the time, this is applied many times in rapid succession)

            // Simple "relax" step to just to reduce excessive clumping. Keep it
            // subtle, because we want to keep some randomness in the placement
            // -- a very strong relax would eventually result in evenly spaced objects
        static float relaxStrength = 0.002f;
        
        std::vector<Float2> adjustment;
        adjustment.resize(workingSet.size(), Zero<Float2>());
        for (auto bi=workingSet.begin(); bi!=workingSet.end(); ++bi) {
            float s = 1.0f / float(workingSet.size());
            float A = (Magnitude(*bi) / radius);
            s *= 1.f - A * A * A;    // (objects near the edges should relax less, otherwise they get moved out of the circle

            for (auto oi=workingSet.begin(); oi!=workingSet.end(); ++oi) {
                if (bi == oi) continue;

                Float2 diff = (*bi) - (*oi);
                adjustment[bi-workingSet.begin()] += diff * (s * relaxStrength * std::log(MagnitudeSquared(diff)));
            }
        }

        for (auto bi=workingSet.begin(); bi!=workingSet.end(); ++bi)
            *bi += adjustment[bi-workingSet.begin()];
    }

    void CalculateScatterOperation(
        std::vector<SceneEngine::PlacementGUID>& _toBeDeleted,
        std::vector<Float3>& _spawnPositions,
        SceneEngine::PlacementsEditor& editor,
        const SceneEngine::IIntersectionScene& hitTestScene,
        const char* const* modelName, unsigned modelCount,
        const Float3& centre, float radius, float density)
    {
        if (!modelCount) return;

        std::vector<uint64> modelGuids;
        for (unsigned c=0; c<modelCount; ++c)
            modelGuids.push_back(Hash64(modelName[c]));
        std::sort(modelGuids.begin(), modelGuids.end());

            // Our scatter algorithm is a little unique
            //  * find the number of objects with the same model & material within 
            //      a cylinder around the given point
            //  * either add or remove one from that total
            //  * create a new random distribution of items around the centre point
            //  * clamp those items to the terrain surface
            //  * delete the old items, and add those new items to the terrain
            // This way items will usually get scattered around in a good distribution
            // But it's very random. So the artist has only limited control.

        std::vector<Float2> noisyPts;

        _toBeDeleted = editor.GetManager()->GetIntersections()->Find_BoxIntersection(
            editor.GetCellSet(),
            centre - Float3(radius, radius, radius),
            centre + Float3(radius, radius, radius),
            [radius, centre, &modelGuids, &noisyPts](const SceneEngine::PlacementsIntersections::IntersectionDef& objectDef) -> bool
            {
                auto i = std::lower_bound(modelGuids.cbegin(), modelGuids.cend(), objectDef._model);
                if (i != modelGuids.cend() && *i == objectDef._model) {
                        // Make sure the object bounding box intersects with a cylinder around "centre"
                        // box vs cylinder is a little expensive. But since the cylinder axis is just +Z
                        // perhaps we could just treat this as a 2d problem, and just do circle vs rhomboid
                        //
                        // This won't produce the same result as cylinder vs box for the caps of the cylinder
                        //      -- but we don't really care in this case.
                    auto localToCircle = Combine(objectDef._localToWorld, AsFloat3x4(-centre));
                    if (AABBVsCircleInXY(radius, objectDef._localSpaceBoundingBox, localToCircle)) {
                        noisyPts.push_back(Truncate(ExtractTranslation(localToCircle)));
                        return true;
                    }
                }

                return false;
            });

            //  Note that the blur noise method we're using will probably not work 
            //  well with very small numbers of placements. So we're going to limit 
            //  the bottom range.
            //  We will use the first model in the list to calibrate the scatter densitys

        auto modelBoundingBox = editor.GetModelBoundingBox(modelName[0]);
        if (    modelBoundingBox.second[0] <= modelBoundingBox.first[0]
            ||  modelBoundingBox.second[1] <= modelBoundingBox.first[1]
            ||  modelBoundingBox.second[2] <= modelBoundingBox.first[2])
            return;

        auto crossSectionArea = 
              (modelBoundingBox.second[0] - modelBoundingBox.first[0]) 
            * (modelBoundingBox.second[1] - modelBoundingBox.first[1]);

		static std::mt19937 generator(std::random_device().operator()());

            // randomly remove one existing object
        if (!noisyPts.empty()) {
			// EraseRandomPoints(noisyPts, noisyPts.size()-1, generator);
			// The biased erase it very expensive when removing a single object. We can do
			// better by looking for a point within 3/4 of the radius.
			float eraseLimit = 3.f/4.f * radius;
			unsigned iterationCount = 0;
			for (;;) {
				auto index = std::uniform_int_distribution<size_t>(0, noisyPts.size()-1)(generator);
				if (MagnitudeSquared(noisyPts[index]) < eraseLimit*eraseLimit || iterationCount > 100) {
					noisyPts.erase(noisyPts.begin() + index);
					break;
				}
				iterationCount++;
			}
		}

        float bigCircleArea = gPI * radius * radius;
        GenerateBlueNoisePlacements(noisyPts, radius, unsigned(bigCircleArea*density/crossSectionArea), generator);

            //  Now add new placements for all of these pts.
            //  We need to clamp them to the terrain surface as we do this

        for (auto p=noisyPts.begin(); p!=noisyPts.end(); ++p) {
            Float2 pt = *p + Truncate(centre);
            float height = 0.f;
            auto terrainHeightQuery = SceneEngine::Stubs::GetTerrainHeight(hitTestScene, pt);
            if (terrainHeightQuery.has_value())
                height = terrainHeightQuery.value();

            _spawnPositions.push_back(Expand(pt, height));
        }
    }

    void ScatterPlacements::PerformScatter(
        const SceneEngine::IIntersectionScene& hitTestScene,
        const Float3& centre, const char modelName[], const char materialName[])
    {
        std::vector<SceneEngine::PlacementGUID> toBeDeleted;
        std::vector<Float3> spawnPositions;

        CalculateScatterOperation(
            toBeDeleted, spawnPositions, 
            *_editor.get(), hitTestScene,
            &modelName, 1,
            centre, _radius, _density);

            //  We have a list of placements using the same model, and within the placement area.
            //  We want to either add or remove one.

        auto trans = _editor->Transaction_Begin(
            AsPointer(toBeDeleted.cbegin()), AsPointer(toBeDeleted.cend()));
        for (unsigned c=0; c<trans->GetObjectCount(); ++c) {
            trans->Delete(c);
        }
            
        for (auto p=spawnPositions.begin(); p!=spawnPositions.end(); ++p) {
            auto objectToWorld = AsFloat4x4(*p);
            Combine_IntoRHS(RotationZ(rand() * 2.f * gPI / float(RAND_MAX)), objectToWorld);
            trans->Create(SceneEngine::PlacementsEditor::ObjTransDef(
                AsFloat3x4(objectToWorld), modelName, materialName, nullptr));
        }

        trans->Commit();
    }

    void ScatterPlacements::Render(
        RenderCore::IThreadContext& context,
        RenderCore::Techniques::ParsingContext& parserContext,
        RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAccelerators)
    {
        if (_hasHoverPoint)
            RenderCylinderHighlight(context, parserContext, pipelineAccelerators, _hoverPoint, _radius);
    }

    const char* ScatterPlacements::GetName() const  { return "ScatterPlace"; }

    auto ScatterPlacements::GetFloatParameters() const -> IteratorRange<const FloatParameter*> 
    {
        static FloatParameter parameters[] = 
        {
			FloatParameter{ManipulatorParameterOffset(&ScatterPlacements::_radius), 1.f, 100.f, FloatParameter::ScaleType::Logarithmic, "Size"},
			FloatParameter{ManipulatorParameterOffset(&ScatterPlacements::_density), 0.01f, 1.f, FloatParameter::ScaleType::Linear, "Density"}
        };
        return MakeIteratorRange(parameters);
    }

	auto ScatterPlacements::GetBoolParameters() const -> IteratorRange<const BoolParameter*>    { return {}; }

    void ScatterPlacements::SetActivationState(bool newState) 
    {
        if (_manInterface) {
            _manInterface->EnableSelectedModelDisplay(newState);
        }
    }

    ScatterPlacements::ScatterPlacements(
        IPlacementManipulatorSettings* manInterface,
        std::shared_ptr<SceneEngine::PlacementsEditor> editor)
    {
        _spawnTimer = {};
        _manInterface = manInterface;
        _editor = std::move(editor);
        _hasHoverPoint = false;
        _hoverPoint = Float3(0.f, 0.f, 0.f);
        _radius = 20.f;
        _density = .1f;
    }
    
    ScatterPlacements::~ScatterPlacements() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    std::vector<std::unique_ptr<IManipulator>> CreatePlacementManipulators(
        IPlacementManipulatorSettings* context,
        std::shared_ptr<SceneEngine::PlacementsEditor> editor,
        std::shared_ptr<SceneEngine::PlacementsRenderer> renderer)
    {
        std::vector<std::unique_ptr<IManipulator>> result;
        result.push_back(std::make_unique<SelectAndEdit>(context, editor, renderer));
        result.push_back(std::make_unique<PlaceSingle>(context, editor, renderer));
        result.push_back(std::make_unique<ScatterPlacements>(context, editor));
        return result;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    static const auto Id_SelectedModel = InteractableId_Make("SelectedModel");
    static const auto Id_PlacementsSave = InteractableId_Make("PlacementsSave");

    void PlacementsWidgets::Render(
        IOverlayContext& context, Layout& layout, 
        Interactables& interactables, InterfaceState& interfaceState)
    {
        auto controlsRect = DrawManipulatorControls(
            context, layout, interactables, interfaceState,
            *_manipulators[_activeManipulatorIndex], "Placements tools");


            //      Our placement display is simple... 
            //
            //      Draw the name of the selected model at the bottom of
            //      the screen. If we click it, we should pop up the model browser
            //      so we can reselect.

        // const auto lineHeight   = 20u;
        // const auto horizPadding = 75u;
        // const auto vertPadding  = 50u;
        // const auto selectedRectHeight = lineHeight + 20u;
        // auto maxSize = layout.GetMaximumSize();
        // Rect selectedRect(
        //     Coord2(maxSize._topLeft[0] + horizPadding, maxSize._bottomRight[1] - vertPadding - selectedRectHeight),
        //     Coord2(std::min(maxSize._bottomRight[0], controlsRect._bottomRight[1]) - horizPadding, maxSize._bottomRight[1] - vertPadding));
        // 
        // DrawButtonBasic(
        //     context, selectedRect, _selectedModel->_modelName.c_str(),
        //     FormatButton(interfaceState, Id_SelectedModel, 
        //         ButtonNormalState, ButtonMouseOverState, ButtonPressedState));
        // interactables.Register({selectedRect, Id_SelectedModel});
        
        auto maxSize = layout.GetMaximumSize();
        int previewSize = 196;
        auto* modelBrowser = dynamic_cast<Overlays::IModelBrowser*>(_browser.get());
        if (modelBrowser)
            previewSize = modelBrowser->GetPreviewSize()[1];
        const auto margin = layout._paddingInternalBorder;
        const Coord2 iconSize(93/2, 88/2);
        const auto iconPadding = layout._paddingBetweenAllocations;
        const auto buttonsRectPadding = 16;
        const auto buttonsRectVertPad = 4;

        const bool browserActive = _browser && _browserActive;
        if (!browserActive) {

                ///////////////////////   I C O N S   ///////////////////////

            const char* icons[] = 
            {
                "xleres/DefaultResources/icon_save.png",
                "xleres/DefaultResources/icon_test.png",
                "xleres/DefaultResources/icon_test.png",
            };
            const InteractableId iconIds[] = { Id_PlacementsSave, 0, 0 };
            const auto iconCount = dimof(icons);
            const auto toolAreaWidth = std::max(previewSize, int(iconCount * iconSize[0] + (iconCount-1) * iconPadding + 2 * buttonsRectPadding));

            const Rect buttonsArea(
                Coord2(margin, maxSize._bottomRight[1] - margin - iconSize[1] - 2 * buttonsRectVertPad),
                Coord2(margin + toolAreaWidth, maxSize._bottomRight[1] - margin));

            context.DrawQuad(
                ProjectionMode::P2D, 
                AsPixelCoords(Coord2(buttonsArea._topLeft[0], LinearInterpolate(buttonsArea._topLeft[1], buttonsArea._bottomRight[1], 0.5f))),
                AsPixelCoords(buttonsArea._bottomRight), ColorB(0, 0, 0));

            for (unsigned c=0; c<iconCount; ++c) {
                Coord2 iconTopLeft(buttonsArea._topLeft + Coord2(buttonsRectPadding + c * (iconSize[0] + iconPadding), buttonsRectVertPad));
                Rect iconRect(iconTopLeft, iconTopLeft + iconSize);

                auto texture = ::Assets::Actualize<RenderCore::Techniques::DeferredShaderResource>(icons[c]);
                context.RequireCommandList(texture->GetCompletionCommandList());
                context.DrawTexturedQuad(
                    ProjectionMode::P2D, 
                    AsPixelCoords(iconRect._topLeft),
                    AsPixelCoords(iconRect._bottomRight),
                    texture->GetShaderResource());
                if (iconIds[c]) {
                    interactables.Register({iconRect, iconIds[c]});
                }
            }
        
                ///////////////////////   S E L E C T E D    I T E M   ///////////////////////

            if (!_selectedModel.empty() && _drawSelectedModel) {
                const auto centre = buttonsArea._topLeft + Coord2(toolAreaWidth/2, -margin - previewSize/2);
                const auto temp = centre - Coord2(previewSize/2, previewSize/2);
                const Rect previewRect(temp, temp + Coord2(previewSize, previewSize));

				assert(0);		// depreciated because we can't get the device context from IOverlayContext anymore
                /*
				if (_browser) {
                    auto* devContext = context.GetDeviceContext();
                    const RenderCore::Metal::ShaderResourceView* srv = nullptr;
                
                    const char* errorMsg = nullptr;

                    TRY {
                        ucs2 ucs2Filename[MaxPath];
                        Conversion::Convert(ucs2Filename, dimof(ucs2Filename), _selectedModel);

                        auto browserSRV = _browser->GetSRV(*devContext, ucs2Filename);
                        srv = browserSRV.first;
                    } 
                    CATCH(const ::Assets::Exceptions::InvalidAsset&) { errorMsg = "Invalid"; } 
                    CATCH(const ::Assets::Exceptions::PendingAsset&) { errorMsg = "Pending"; } 
                    CATCH_END

                    if (srv) {
                        DrawQuadDirect(
                            *devContext, *srv,
                            Float2(float(previewRect._topLeft[0]), float(previewRect._topLeft[1])), 
                            Float2(float(previewRect._bottomRight[0]), float(previewRect._bottomRight[1])));
                    } else if (errorMsg) {
                        DrawButtonBasic(
                            &context, previewRect, errorMsg,
                            FormatButton(interfaceState, Id_SelectedModel));
                    }
                }
				*/

                interactables.Register({previewRect, Id_SelectedModel});
            }

        } else {

                ///////////////////////   B R O W S E R   ///////////////////////

            Rect browserRect(maxSize._topLeft, maxSize._bottomRight);
            Layout browserLayout(browserRect);
            _browser->Render(context, browserLayout, interactables, interfaceState);

        }
    }

    auto PlacementsWidgets::ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputSnapshot& input) -> ProcessInputResult
    {
        if (interfaceState.TopMostId() == Id_SelectedModel && input.IsRelease_LButton()) {
            _browserActive = !_browserActive;
            return ProcessInputResult::Consumed;
        }

        if (_browser && _browserActive) {
                // disable the browser when pressing escape
            if (input.IsPress(PlatformRig::KeyId_Make("escape"))) {
                _browserActive = false;
                return ProcessInputResult::Consumed;
            }

            auto browser = dynamic_cast<Overlays::IModelBrowser*>(_browser.get());
            if (browser) {
                auto result = browser->SpecialProcessInput(interfaceState, input);
                if (!result._selectedModel.empty()) {
                    _selectedModel = result._selectedModel;
                    _browserActive = false; // dismiss browser on select
                }

                if (result._consumed) { return ProcessInputResult::Consumed; }
            }
        }

        if (input.IsRelease_LButton()) {
            const auto Id_SelectedManipulatorLeft = InteractableId_Make("SelectedManipulatorLeft");
            const auto Id_SelectedManipulatorRight = InteractableId_Make("SelectedManipulatorRight");
            auto topMost = interfaceState.TopMostWidget();
            auto newManipIndex = _activeManipulatorIndex;

            if (topMost._id == Id_SelectedManipulatorLeft) {            // ---- go back one manipulator ----
                newManipIndex = unsigned((_activeManipulatorIndex + _manipulators.size() - 1) % _manipulators.size());
            } else if (topMost._id == Id_SelectedManipulatorRight) {    // ---- go forward one manipulator ----
                newManipIndex = unsigned((_activeManipulatorIndex + 1) % _manipulators.size());
            }

            if (newManipIndex != _activeManipulatorIndex) {
                _manipulators[_activeManipulatorIndex]->SetActivationState(false);
                _activeManipulatorIndex = newManipIndex;
                _manipulators[_activeManipulatorIndex]->SetActivationState(true);
                return ProcessInputResult::Consumed;
            }

            if (topMost._id == Id_PlacementsSave) {
                _editor->WriteAllCells();
                return ProcessInputResult::Consumed;
            }
        }

        if (HandleManipulatorsControls(interfaceState, input, *_manipulators[_activeManipulatorIndex])) {
            return ProcessInputResult::Consumed;
        }

        if (interfaceState.GetMouseOverStack().empty()) {

            const auto& inputContext = interfaceState.GetViewInputContext();
			SceneEngine::IntersectionTestContext intersectionContext {
				AsCameraDesc(*_camera),
				inputContext._viewMins, inputContext._viewMaxs,
				_drawingApparatus };

            if (_manipulators[_activeManipulatorIndex]->OnInputEvent(input, intersectionContext, _intersectionTestScene.get()))
				return ProcessInputResult::Consumed;
        }

        return ProcessInputResult::Passthrough;
    }

    void PlacementsWidgets::RenderToScene(
        RenderCore::IThreadContext& context, 
		RenderCore::Techniques::ParsingContext& parserContext,
        RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAccelerators)
    {
        _manipulators[_activeManipulatorIndex]->Render(context, parserContext, pipelineAccelerators);
    }

    std::string PlacementsWidgets::GetSelectedModel() const { return _selectedModel; }
    std::string PlacementsWidgets::GetSelectedMaterial() const { return _selectedMaterial; }

    void PlacementsWidgets::EnableSelectedModelDisplay(bool newState)
    {
        _drawSelectedModel = newState;
    }

    void PlacementsWidgets::SelectModel(const char newModelName[], const char materialName[])
    {
        if (newModelName && materialName) {
            _selectedModel = newModelName;
            _selectedMaterial = materialName;
        }
    }

    void PlacementsWidgets::SwitchToMode(Mode::Enum newMode)
    {
        auto newActiveManipIndex = _activeManipulatorIndex;
        if (newMode == Mode::PlaceSingle) { newActiveManipIndex = 1; }
        else if (newMode == Mode::Select) { newActiveManipIndex = 0; }

        if (newActiveManipIndex != _activeManipulatorIndex) {
            _manipulators[_activeManipulatorIndex]->SetActivationState(false);
            _activeManipulatorIndex = newActiveManipIndex;
            _manipulators[_activeManipulatorIndex]->SetActivationState(true);
        }
    }

    PlacementsWidgets::PlacementsWidgets(
        const std::shared_ptr<SceneEngine::PlacementsEditor>& editor, 
        const std::shared_ptr<SceneEngine::PlacementsRenderer>& renderer, 
        const std::shared_ptr<SceneEngine::IIntersectionScene>& intersectionTestScene,
		const std::shared_ptr<VisCameraSettings>& camera,
		const std::shared_ptr<RenderCore::Techniques::DrawingApparatus>& drawingApparatus)
    {
        // auto browser = std::make_shared<ModelBrowser>("game\\model");
        _browserActive = false;
        _activeManipulatorIndex = 0;
        _drawSelectedModel = false;

        auto manipulators = CreatePlacementManipulators(this, editor, renderer);
        manipulators[0]->SetActivationState(true);

        _editor = editor;
        _intersectionTestScene = intersectionTestScene;
        _manipulators = std::move(manipulators);
		_camera = camera;
		_drawingApparatus = drawingApparatus;
    }
    
    PlacementsWidgets::~PlacementsWidgets()
    {
        TRY { 
            _manipulators[_activeManipulatorIndex]->SetActivationState(false);
        } CATCH (...) {
        } CATCH_END
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    class PlacementsManipulatorsManager::Pimpl
    {
    public:
        std::shared_ptr<SceneEngine::PlacementsManager> _placementsManager;
        std::shared_ptr<SceneEngine::PlacementsEditor>  _editor;
        std::shared_ptr<DebugScreensSystem>             _screens;
        std::shared_ptr<PlacementsWidgets>              _placementsDispl;
        std::shared_ptr<SceneEngine::IIntersectionScene>     _intersectionTestScene;
        std::shared_ptr<RenderCore::Techniques::IImmediateDrawables> _immediateDrawables;
        std::shared_ptr<RenderOverlays::FontRenderingManager> _fontRenderingManager;
    };

    void PlacementsManipulatorsManager::RenderWidgets(
        RenderCore::IThreadContext& device, RenderCore::Techniques::ParsingContext& parsingContext,
        RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAccelerators)
    {
		auto overlayContext = RenderOverlays::MakeImmediateOverlayContext(device, *_pimpl->_immediateDrawables, _pimpl->_fontRenderingManager.get());
		auto viewportDims = device.GetStateDesc()._viewportDimensions;
		_pimpl->_screens->Render(*overlayContext, RenderOverlays::Rect{ { 0,0 },{ int(viewportDims[0]), int(viewportDims[1]) } });
    }

    void PlacementsManipulatorsManager::RenderToScene(
        RenderCore::IThreadContext& device, 
		RenderCore::Techniques::ParsingContext& parserContext,
        RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAccelerators)
    {
        _pimpl->_placementsDispl->RenderToScene(device, parserContext, pipelineAccelerators);
    }

    auto PlacementsManipulatorsManager::GetInputLister() -> std::shared_ptr<PlatformRig::IInputListener>
    {
        return _pimpl->_screens;
    }

    PlacementsManipulatorsManager::PlacementsManipulatorsManager(
        const std::shared_ptr<SceneEngine::PlacementsManager>& placementsManager,
        const std::shared_ptr<SceneEngine::PlacementCellSet>& placementCellSet,
        const std::shared_ptr<SceneEngine::TerrainManager>& terrainManager,
        const std::shared_ptr<VisCameraSettings>& camera,
        const std::shared_ptr<RenderCore::Techniques::ImmediateDrawingApparatus>& immediateDrawablesApparatus)
    {
        auto pimpl = std::make_unique<Pimpl>();
        pimpl->_screens = std::make_shared<DebugScreensSystem>();
        pimpl->_placementsManager = placementsManager;
        pimpl->_editor = pimpl->_placementsManager->CreateEditor(placementCellSet);
        pimpl->_intersectionTestScene = SceneEngine::CreateIntersectionTestScene(
            terrainManager, placementCellSet, pimpl->_editor);
        pimpl->_placementsDispl = std::make_shared<PlacementsWidgets>(
            pimpl->_editor, placementsManager->GetRenderer(),
            pimpl->_intersectionTestScene, camera, immediateDrawablesApparatus->_mainDrawingApparatus);
        pimpl->_screens->Register(pimpl->_placementsDispl, "Placements", DebugScreensSystem::SystemDisplay);
        pimpl->_immediateDrawables = immediateDrawablesApparatus->_immediateDrawables;
        pimpl->_fontRenderingManager = immediateDrawablesApparatus->_fontRenderingManager;
        _pimpl = std::move(pimpl);
    }

    PlacementsManipulatorsManager::~PlacementsManipulatorsManager()
    {}


///////////////////////////////////////////////////////////////////////////////////////////////////

}

