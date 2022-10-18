// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

using System.ComponentModel.Composition.Hosting;
using Sce.Atf.Dom;

namespace LevelEditorXLE
{
    public static class Startup
    {
        public static void InitializeAdapters()
        {
            Schema.placementsCellReferenceType.Type.Define(new ExtensionInfo<Placements.PlacementsCellRef>());
            Schema.placementsFolderType.Type.Define(new ExtensionInfo<Placements.PlacementsFolder>());
            Schema.placementsDocumentType.Type.Define(new ExtensionInfo<Placements.XLEPlacementDocument>());
            Schema.placementObjectType.Type.Define(new ExtensionInfo<Placements.XLEPlacementObject>());
            Schema.modelBookmarkType.Type.Define(new ExtensionInfo<Placements.Bookmark>());
#if GUILAYER_SCENEENGINE
            Schema.terrainType.Type.Define(new ExtensionInfo<Terrain.XLETerrainGob>());
            Schema.terrainCoverageLayer.Type.Define(new ExtensionInfo<Terrain.XLETerrainCoverage>());
            Schema.terrainBaseTextureType.Type.Define(new ExtensionInfo<Terrain.TerrainBaseTexture>());
            Schema.abstractTerrainMaterialDescType.Type.Define(new ExtensionInfo<Terrain.TerrainBaseTextureMaterial>());
            Schema.vegetationSpawnConfigType.Type.Define(new ExtensionInfo<Terrain.VegetationSpawnConfigGob>());
            Schema.vegetationSpawnMaterialType.Type.Define(new ExtensionInfo<Terrain.VegetationSpawnMaterialItem>());
#endif
            Schema.envSettingsFolderType.Type.Define(new ExtensionInfo<Environment.XLEEnvSettingsFolder>());
            Schema.envSettingsType.Type.Define(new ExtensionInfo<Environment.XLEEnvSettings>());
            Schema.envUtilityType.Type.Define(new ExtensionInfo<Environment.EnvUtility>());
            Schema.envObjectType.Type.Define(new ExtensionInfo<Environment.XLEEnvObject>());
            Schema.xleGameType.Type.Define(new ExtensionInfo<Game.GameExtensions>());
            Schema.triMeshMarkerType.Type.Define(new ExtensionInfo<Markers.TriMeshMarker>());
            Schema.markerPointType.Type.Define(new ExtensionInfo<Markers.PointMarker>());
            Schema.gameObjectFolderType.Type.Define(new ExtensionInfo<Game.XLEGameObjectsFolder>());
        }

        public static TypeCatalog CreateTypeCatalog()
        {
            return new TypeCatalog(
                typeof(LevelEditorXLE.GameLoopService),
                typeof(Manipulators.XLEManipCtrlWin),
                typeof(XLECamera),
                typeof(XLEAssetService), 
                typeof(Commands),

#if GUILAYER_SCENEENGINE
                typeof(Terrain.TerrainManipulator),
#endif

                typeof(ControlsLibraryExt.Commands.CommonCommands),
                typeof(ControlsLibraryExt.Material.MaterialInspector),
                typeof(ControlsLibraryExt.Material.MaterialSchemaLoader),
                typeof(ControlsLibraryExt.Material.ActiveMaterialContext),
                typeof(ControlsLibraryExt.Material.MatTab),
                typeof(ControlsLibraryExt.AssetTrackingService),
                typeof(Materials.PickMaterialManipulator),

                // typeof(Placements.PlacementManipulator),     (provides access to the native placements manipulators... but not really required)
                typeof(Placements.ResourceConverter),
                typeof(Placements.ResourceListerCommandClient),

                typeof(AssetMan.ResourcePreview),
                typeof(Manipulators.ExtraEditCommands),
                typeof(Placements.ScatterPlaceManipulator),
#if GUILAYER_SCENEENGINE
                typeof(Terrain.TerrainNamingBridge),
#endif

                typeof(XLEBridgeUtils.ResourceQueryService)
                );
        }
    }
}
