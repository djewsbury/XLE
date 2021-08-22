// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

using System;
using System.ComponentModel.Composition;
using System.Drawing;
using System.Windows.Forms;

using Sce.Atf;
using Sce.Atf.Applications;
using Sce.Atf.VectorMath;

using Camera = Sce.Atf.Rendering.Camera;
using ViewTypes = Sce.Atf.Rendering.ViewTypes;

using LevelEditorCore;

namespace LevelEditorXLE
{

    [Export(typeof(CameraController))]
    [PartCreationPolicy(CreationPolicy.Shared)]
    public class XLECamera : CameraController
    {
        public XLECamera()
        {
            CommandInfo = new CommandInfo(
                   this,
                   StandardMenu.View,
                   StandardCommandGroup.ViewCamera,
                   "camera".Localize() + "/" + "XLE".Localize(),
                   "XLE orbit and pan camera".Localize(),
                   Sce.Atf.Input.Keys.None,
                   LevelEditorCore.Resources.MayaImage,
                   CommandVisibility.Menu);
        }

        public override bool MouseDown(object sender, MouseEventArgs e)
        {
            if (InputScheme.ActiveControlScheme.IsControllingCamera(KeysInterop.ToAtf(Control.ModifierKeys), MouseEventArgsInterop.ToAtf(e)))
            {
                m_lastMousePointX = e.Location.X;
                m_lastMousePointY = e.Location.Y;
                m_dragging = true;
                return true;
            }

            if (Control.ModifierKeys.HasFlag(Keys.Control) && !Control.ModifierKeys.HasFlag(Keys.Shift) && e.Button == MouseButtons.Left)
            {
                // "control + l click" repositions the "focus" point of the camera
                //      -- it's just incredibly useful to be able to manually set the point, because it
                //          allows the user to specify both the speed of the movement of the camera and
                //          the orbit of the camera in a natural way
                //
                // We could expand "ActiveControlScheme" to allow this key binding to be rebound...
                // but just using fixed binding for now.
                // This is a very important key combination (it's just really useful to be able to move
                // the focus around quickly) -- so it should be accessable with any easy combination from
                // anywhere!

                ViewControl c = sender as ViewControl;
                GUILayer.IViewContext vc = sender as GUILayer.IViewContext;
                if (c != null && vc != null) {
                        // We can use XLEBridgeUtils to do the ray test. This will
                        // execute the native code (which in turn performs the intersection
                        // on the GPU)
                        // Note that we're using the more complex picking interface because
                        // we want to use explicitly pass "Camera" (rather than
                        // getting it from the view control)
                    var hit = XLEBridgeUtils.Picking.RayPick(
                        GUILayer.EngineDevice.GetInstance(),
                        vc.SceneManager,
                        c.GetWorldRay(e.Location), XLEBridgeUtils.Utils.AsCameraDesc(Camera), c.ClientSize,
                        XLEBridgeUtils.Picking.Flags.AllWorldObjects);
                    if (hit != null && hit.Length > 0)
                    {
                        Vec3F transformedPt;
                        Camera.AxisSystem.Transform(hit[0].hitPt, out transformedPt);
                        Camera.Set(Camera.Eye, transformedPt, Camera.Up);
                    }
                }

                return true;
            }

            return base.MouseDown(sender, e);
        }

        private static Vec3F CartesianToSphericalYUp(Vec3F cartesian) 
        {
            float length = cartesian.Length;
            return new Vec3F(
                (float)Math.Acos(cartesian.Y / length),
                (float)Math.Atan2(cartesian.Z, cartesian.X),
                length);
        }

        private static Vec3F SphericalToCartesianYUp(Vec3F spherical) 
        {
            return new Vec3F(
                (float)(spherical[2] * Math.Sin(spherical[0]) * Math.Cos(spherical[1])),
                (float)(spherical[2] * Math.Cos(spherical[0])),
                (float)(spherical[2] * Math.Sin(spherical[0]) * Math.Sin(spherical[1])));
        }

        private static float Clamp(float value, float min, float max) 
        {
            return Math.Max(Math.Min(value, max), min);
        }

        public override bool MouseMove(object sender, MouseEventArgs e)
        {
            if (m_dragging &&
                InputScheme.ActiveControlScheme.IsControllingCamera(KeysInterop.ToAtf(Control.ModifierKeys), MouseEventArgsInterop.ToAtf(e)))
            {
                Control c = sender as Control;
                float dx = e.X - m_lastMousePointX;
                float dy = e.Y - m_lastMousePointY;

                if (InputScheme.ActiveControlScheme.IsRotating(KeysInterop.ToAtf(Control.ModifierKeys), MouseEventArgsInterop.ToAtf(e)) &&
                    (Camera.ViewType == ViewTypes.Perspective || LockOrthographic == false))
                {
                    var orbitRotationSpeed = .0005f * (float)Math.PI;

                        // just do this orbitting calculation in spherical coords...
                        // it's much easier than any other method
                    var orbitCenter = Camera.LookAtPoint;
                    var spherical = CartesianToSphericalYUp(orbitCenter - Camera.Eye);
                    spherical[0] += dy * orbitRotationSpeed;
                    spherical[0] = Clamp(spherical[0], (float)Math.PI * 0.02f, (float)Math.PI * 0.98f);
                    spherical[1] += dx * orbitRotationSpeed;

                    var defaultUp = new Vec3F(0.0f, 1.0f, 0.0f);    // (geometry gets hopelessly unnormalized if we don't reset default-up here)
                    Camera.Set(orbitCenter - SphericalToCartesianYUp(spherical), orbitCenter, defaultUp);
                    if (Camera.ViewType != ViewTypes.Perspective)
                        Camera.ViewType = ViewTypes.Perspective;
                }
                else if (InputScheme.ActiveControlScheme.IsZooming(KeysInterop.ToAtf(Control.ModifierKeys), MouseEventArgsInterop.ToAtf(e)))
                {
                    float zoom = (-dy - dx);

                    float adj = Camera.DistanceFromLookAt;
                    var zoomSpeed = 0.01f * adj;
                    var lookAtDir = Vec3F.Normalize(Camera.LookAt);
                    var movement = Math.Min(zoom * zoomSpeed, Camera.DistanceFromLookAt - 0.1f);
                    Camera.Set(Camera.Eye + lookAtDir * movement, Camera.LookAtPoint, Camera.Up);
                }
                else if (InputScheme.ActiveControlScheme.IsPanning(KeysInterop.ToAtf(Control.ModifierKeys), MouseEventArgsInterop.ToAtf(e)))
                {
                    float adj = Camera.DistanceFromLookAt;
                    var panSpeed = 0.001f * adj;
                    var lookAtPoint = Camera.LookAtPoint;
                    var translation = (Camera.Up * dy * panSpeed) + (Camera.Right * -dx * panSpeed);
                    Camera.Set(Camera.Eye + translation, lookAtPoint + translation, Camera.Up);
                }

                m_lastMousePointX = e.Location.X;
                m_lastMousePointY = e.Location.Y;
                return true;
            }

            return base.MouseMove(sender, e);
        }

        public override bool MouseUp(object sender, MouseEventArgs e)
        {
            if (m_dragging)
            {
                m_dragging = false;
                return true;
            }
            return base.MouseUp(sender, e);
        }

        public override bool MouseWheel(object sender, MouseEventArgs e)
        {
            if (!InputScheme.ActiveControlScheme.IsZooming(KeysInterop.ToAtf(Control.ModifierKeys), MouseEventArgsInterop.ToAtf(e)))
                return true;

            float adj = Camera.DistanceFromLookAt;
            var zoomSpeed = .1f / 120.0f * adj;
            var lookAtDir = Vec3F.Normalize(Camera.LookAt);
            var movement = Math.Min(e.Delta * zoomSpeed, Camera.DistanceFromLookAt - 0.1f);
            Camera.Set(Camera.Eye + lookAtDir * movement, Camera.LookAtPoint, Camera.Up);
            return true;
        }

        protected override void CameraToController(Camera camera)
        {
            base.CameraToController(camera);
        }

        protected override void ControllerToCamera(Camera camera)
        {
            base.ControllerToCamera(camera);
        }

        private float m_lastMousePointX;
        private float m_lastMousePointY;
        private bool m_dragging;
    }

}
