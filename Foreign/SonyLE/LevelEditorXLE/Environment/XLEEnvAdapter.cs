// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

using System;
using System.Collections.Generic;
using System.Linq;
using System.ComponentModel;

using Sce.Atf.Adaptation;
using Sce.Atf.Applications;
using Sce.Atf.Dom;

using LevelEditorCore;
using LevelEditorXLE.Extensions;

namespace LevelEditorXLE.Environment
{
    public class XLEEnvSettings : DomNodeAdapter, IHierarchical, IListable, ICommandClient, IContextMenuCommandProvider
    {
        #region IHierachical implementation
        public bool CanAddChild(object child)
        {
            var domNode = child.As<DomNode>();
            if (domNode != null)
            {
                foreach (var type in domNode.Type.Lineage)
                {
                    if (type == Schema.envObjectType.Type) return true;
                    if (type == Schema.envMiscType.Type) return true;
                    if (type == Schema.ambientSettingsType.Type) return true;
                    if (type == Schema.toneMapSettingsType.Type) return true;
                    if (type == Schema.distantIBLType.Type) return true;
                }
            }
            return false;
        }

        public bool AddChild(object child)
        {
            var domNode = child.As<DomNode>();
            if (domNode != null)
            {
                foreach (var type in domNode.Type.Lineage)
                {
                    if (type == Schema.envObjectType.Type)
                    {
                        GetChildList<DomNode>(Schema.envSettingsType.objectsChild).Add(domNode);
                        return true;
                    }
                    if (type == Schema.envMiscType.Type)
                    {
                        GetChildList<DomNode>(Schema.envSettingsType.settingsChild).Add(domNode);
                        return true;
                    }
                    if (type == Schema.ambientSettingsType.Type)
                    {
                        SetChild(Schema.envSettingsType.ambientChild, domNode);
                        return true;
                    }
                    if (type == Schema.toneMapSettingsType.Type)
                    {
                        SetChild(Schema.envSettingsType.tonemapChild, domNode);
                        return true;
                    }
                    if (type == Schema.distantIBLType.Type)
                    {
                        SetChild(Schema.envSettingsType.distantIBLChild, domNode);
                        return true;
                    }
                }
            }
            return false;
        }
        #endregion

        public DomNode FindObjectByName(string name)
        {
            var objects = GetChildList<DomNode>(Schema.envSettingsType.objectsChild);
            foreach (var o in objects)
            {
                var oName = o.GetAttribute(Schema.envObjectType.nameAttribute) as string;
                if (oName != null && oName.Equals(name))
                    return o;
            }
            return null;
        }

        public void GetInfo(ItemInfo info)
        {
            info.ImageIndex = Util.GetTypeImageIndex(DomNode.Type, info.GetImageList());
            info.Label = "Settings: " + Name;
        }

        public string Name
        {
            get { return GetAttribute<string>(Schema.envSettingsType.NameAttribute); }
        }

        public static DomNode Create(string name)
        {
            var result = new DomNode(Schema.envSettingsType.Type);
            result.SetAttribute(Schema.envSettingsType.NameAttribute, name);
            return result;
        }

        bool ICommandClient.CanDoCommand(object commandTag)
        {
            if (!(commandTag is Command)) return false;

            switch ((Command)commandTag)
            {
                case Command.AddAmbientSettings:
                    return GetChild<DomNode>(Schema.envSettingsType.ambientChild) == null;

                case Command.AddToneMapSettings:
                    return GetChild<DomNode>(Schema.envSettingsType.tonemapChild) == null;

                case Command.AddDistantIBLSettings:
                    return GetChild<DomNode>(Schema.envSettingsType.distantIBLChild) == null;

                case Command.AddSun:
                    return FindObjectByName("Sun") == null;

                case Command.AddAreaLight:
                case Command.AddDirectionalLight:
                case Command.AddSunSourceShadowSettings:
                    return true;
            }

            return false;
        }

        void ICommandClient.DoCommand(object commandTag)
        {
            if (!(commandTag is Command)) return;

            switch ((Command)commandTag)
            {
                case Command.AddAmbientSettings:
                    ApplicationUtil.Insert(
                        DomNode.GetRoot(), this,
                        new DomNode(Schema.ambientSettingsType.Type),
                        "Add Ambient Settings", null);
                    break;

                case Command.AddToneMapSettings:
                    ApplicationUtil.Insert(
                        DomNode.GetRoot(), this,
                        new DomNode(Schema.toneMapSettingsType.Type),
                        "Add Tone Map Settings", null);
                    break;

                case Command.AddDistantIBLSettings:
                    ApplicationUtil.Insert(
                        DomNode.GetRoot(), this,
                        new DomNode(Schema.distantIBLType.Type),
                        "Add Distant IBL Settings", null);
                    break;

                case Command.AddSun:
                    {
                            // the "Sun" is just a directional light with the name "Sun"
                        var sun = new DomNode(Schema.directionalLightType.Type);
                        sun.SetAttribute(Schema.envObjectType.nameAttribute, "Sun");
                        ApplicationUtil.Insert(DomNode.GetRoot(), this, sun, "Add Sun", null);
                        ApplicationUtil.Insert(
                            DomNode.GetRoot(), this,
                            new DomNode(Schema.envUtilityType.Type),
                            "Add Environment Utility", null);
                        break;
                    }

                case Command.AddAreaLight:
                    ApplicationUtil.Insert(
                        DomNode.GetRoot(), this,
                        new DomNode(Schema.areaLightType.Type),
                        "Add Area Light", null);
                    break;

                case Command.AddDirectionalLight:
                    ApplicationUtil.Insert(
                        DomNode.GetRoot(), this,
                        new DomNode(Schema.directionalLightType.Type),
                        "Add Directional Light", null);
                    break;

                case Command.AddSunSourceShadowSettings:
                    ApplicationUtil.Insert(
                        DomNode.GetRoot(), this,
                        new DomNode(Schema.sunSourceShadowSettings.Type),
                        "Add Sun Source Shadow Settings", null);
                    break;
            }
        }

        void ICommandClient.UpdateCommand(object commandTag, CommandState commandState)
        {
        }

        private enum Command
        {
            [Description("Add Ambient Settings")] AddAmbientSettings,
            [Description("Add Tone Map Settings")] AddToneMapSettings,
            [Description("Add Distant IBL Settings")] AddDistantIBLSettings,
            [Description("Add Sun")] AddSun,
            [Description("Add Directional Light")] AddDirectionalLight,
            [Description("Add Area Light")] AddAreaLight,
            [Description("Add Sun Source Shadow Settings")] AddSunSourceShadowSettings
        }

        IEnumerable<object> IContextMenuCommandProvider.GetCommands(object context, object target)
        {
            foreach (Command command in Enum.GetValues(typeof(Command)))
            {
                yield return command;
            }
        }
    }

    public class XLEEnvSettingsFolder : DomNodeAdapter, IHierarchical, IListable, IExportable
    {
        public void GetInfo(ItemInfo info)
        {
            info.ImageIndex = Util.GetTypeImageIndex(DomNode.Type, info.GetImageList());
            info.Label = "EnvSettingsFolder";
        }

        #region IHierachical implementation
        public bool CanAddChild(object child)
        {
            if (child.Is<XLEEnvSettings>()) return true;
            var defEnv = DefaultEnvSettings;
            return (defEnv != null) && defEnv.CanAddChild(child);
        }

        public bool AddChild(object child)
        {
            var settings = child.As<XLEEnvSettings>();
            if (settings != null) {
                Settings.Add(settings);
                return true;
            }
            var defEnv = DefaultEnvSettings;
            if (defEnv != null && defEnv.AddChild(child))
                return true;
            return false;
        }
        #endregion

        public System.Collections.Generic.ICollection<XLEEnvSettings> Settings
        {
            get
            {
                return GetChildList<XLEEnvSettings>(Schema.envSettingsFolderType.settingsChild);
            }
        }

        public XLEEnvSettings DefaultEnvSettings
        {
            get
            {
                return Settings.FirstOrDefault();
            }
        }
        
        #region IExportable
        public Uri ExportTarget
        {
            get
            {
                var fn = GetAttribute<string>(Schema.envSettingsFolderType.ExportTargetAttribute);
                var parent = DomNode.GetRoot().As<Game.GameExtensions>();
                if (parent != null) return new Uri(parent.ExportDirectory, fn);
                return new Uri(fn);
            }
        }

        public string ExportCategory
        {
            get { return "Environment Settings"; }
        }

        public IEnumerable<PendingExport> BuildPendingExports()
        {
            var result = new List<PendingExport>();
            result.Add(new PendingExport(ExportTarget, this.GetSceneManager().ExportEnv(0)));
            return result;
        }
        #endregion

        public string GetNameForNewChild()
        {
            var settings = Settings;
            string result = "environment";
            if (!settings.Any(s => s.Name.Equals(result)))
                return result;

            uint postfix = 1;
            for (; ; ++postfix)
            {
                result = String.Format("environment{0}", postfix);
                if (!settings.Any(s => s.Name.Equals(result)))
                    return result;
            }
        }

        public static DomNode Create()
        {
            return new DomNode(Schema.envSettingsFolderType.Type);
        }
    }

    public class XLEEnvObject : DomNodeAdapter, INameable, IListable
    {
        public virtual string Name
        {
            get
            {
                var result = GetAttribute<string>(Schema.envObjectType.nameAttribute);
                if (string.IsNullOrEmpty(result)) return "<<unnamed>>";
                return result;
            }
            set { SetAttribute(Schema.envObjectType.nameAttribute, value); }
        }

        public void GetInfo(ItemInfo info)
        {
            info.ImageIndex = Util.GetTypeImageIndex(DomNode.Type, info.GetImageList());
            if (DomNode.Type == Schema.directionalLightType.Type)
            {
                info.Label = "Directional: " + Name;
            }
            else if (DomNode.Type == Schema.areaLightType.Type)
            {
                info.Label = "Area: " + Name;
            }
            else
                info.Label = "Obj: " + Name;

            var lockable = this.As<ILockable>();
            if (lockable != null && lockable.IsLocked)
                info.StateImageIndex = info.GetImageList().Images.IndexOfKey(Sce.Atf.Resources.LockImage);
        }
    }
}