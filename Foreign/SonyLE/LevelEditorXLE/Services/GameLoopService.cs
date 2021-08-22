using System;
using System.ComponentModel.Composition;
using System.Drawing;
using System.Windows.Forms;
using System.Runtime.InteropServices;

using Sce.Atf;
using Sce.Atf.Applications;
using Sce.Atf.Adaptation;

using LevelEditorCore;
using System.Diagnostics;

namespace LevelEditorXLE
{
    /// <summary>
    /// Game loop driver.
    /// It continously call update/render as long as 
    /// there is no message in the windows message queue</summary>    
    [Export(typeof(IInitializable))]
    [Export(typeof(IGameLoop))]
    [PartCreationPolicy(CreationPolicy.Shared)]
    public class GameLoopService : LevelEditorCore.IGameLoop, IInitializable
    {

        public GameLoopService()
        {
            // Initilize variables used by GameLoop.
            m_lastUpdateTime = Timing.GetHiResCurrentTime() - UpdateStep;
            m_lastRenderTime = m_lastUpdateTime;
            UpdateType = UpdateType.Paused;
        }

        #region IInitializable Members
        void IInitializable.Initialize()
        {
            Application.Idle += Application_Idle;
        }

        #endregion

        [DllImport("user32.dll", CharSet = CharSet.Auto, ExactSpelling = true)]
        private static extern IntPtr GetForegroundWindow();

        [DllImport("user32.dll", CharSet = CharSet.Auto, SetLastError = true)]
        private static extern int GetWindowThreadProcessId(IntPtr handle, out int processId);

        private bool ApplicationIsActive()
        {
            // Check to see if this application is still in the foreground
            // If we drop into the background, we should suppress updates.
            var foregroundWindow = GetForegroundWindow();
            if (foregroundWindow == IntPtr.Zero) return false;

            int foreWindowProcess;
            GetWindowThreadProcessId(foregroundWindow, out foreWindowProcess);
            return foreWindowProcess == Process.GetCurrentProcess().Id;
        }

        private void Application_Idle(object sender, EventArgs e)
        {
            while (IsIdle())
            {
                if (!ApplicationIsActive())
                {
                    System.Threading.Thread.Sleep(16);
                    return;
                }
                GUILayer.EngineControl.TickRegularAnimation();
                // Update();
                // Render();
            }
        }

        private bool IsIdle()
        {
            return PeekMessage(out m_msg, IntPtr.Zero, 0, 0, 0) == 0;
        }

        ///<summary>Windows Message</summary>
        [StructLayout(LayoutKind.Sequential)]
        private struct Message
        {
            public IntPtr hWnd;
            public uint msg;
            public IntPtr wParam;
            public IntPtr lParam;
            public uint time;
            public Point p;
        }

        [System.Security.SuppressUnmanagedCodeSecurity]
        [DllImport("User32.dll", CharSet = CharSet.Unicode)]
        private static extern int PeekMessage(out Message msg, IntPtr hWnd, uint messageFilterMin, uint messageFilterMax, uint flags);
        private Message m_msg;

        #region IGameLoop members and related code
        public UpdateType UpdateType
        {
            get;
            set;
        }

        public void Update()
        {
            var context = m_designView.Context;
            if (context == null) return;

            // m_gameEngine.SetGameWorld(context.Cast<IGame>());

            var now = Timing.GetHiResCurrentTime();
            double step = now - m_lastUpdateTime;
            m_lastUpdateTime = now;
            FrameTime fr = new FrameTime(m_simulationTime, (float)step);
            m_gameEngine.Update(fr, UpdateType);
            m_simulationTime = +step;
        }

        public void Render()
        {
            // set upper limit of rendering frequency to 1/UpdateStep
            var now = Timing.GetHiResCurrentTime();
            var rdt = now - m_lastRenderTime;
            foreach (var view in m_designView.Views)
                view.Render();
            m_lastRenderTime = now;
        }

        [Import(AllowDefault = false)]
        private IGameEngineProxy m_gameEngine;

        [Import(AllowDefault = false)]
        private IDesignView m_designView;

        private double m_simulationTime;
        private double m_lastRenderTime;
        private double m_lastUpdateTime;
        private double m_updateLagRemainder;
        private const double UpdateStep = 1.0 / 60.0;
        private ToolStripComboBox m_updateTypeComboBox;
        #endregion
    }
}


