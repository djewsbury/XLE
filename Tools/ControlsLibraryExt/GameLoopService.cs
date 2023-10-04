using System;
using System.ComponentModel.Composition;
using System.ComponentModel.Composition.Hosting;
using System.Windows.Forms;
using System.Runtime.InteropServices;

using Sce.Atf;
using Sce.Atf.Adaptation;
using Sce.Atf.Applications;
using Sce.Atf.Controls;
using Sce.Atf.Controls.Adaptable;
using System.Diagnostics;

namespace ControlsLibraryExt
{
    [Export(typeof(IInitializable))]
    [PartCreationPolicy(CreationPolicy.Shared)]
    public class GameLoopService : IInitializable
    {
        void IInitializable.Initialize()
        {
            Application.Idle += Application_Idle;
            _frameToFrameMinimum = Stopwatch.Frequency / 60;
        }

        private void Application_Idle(object sender, EventArgs e)
        {
            while (GUILayer.EngineControl.HasRegularAnimationControls() && IsIdle())
            {
                if (!ApplicationIsActive())
                {
                    System.Threading.Thread.Sleep(16);
                }

                // note -- this is a non-ideal frame scheduling method; it's only meant to prevent rendering from happening too often
                long timer = Stopwatch.GetTimestamp();
                if ((timer - _lastIdleRender) > _frameToFrameMinimum)
                {
                    GUILayer.EngineControl.TickRegularAnimation();
                    _lastIdleRender = timer;
                }
                else
                {
                    System.Threading.Thread.Sleep(0);       // give up some thread time to avoid a busy loop
                }
            }
        }

        private bool IsIdle()
        {
            return PeekMessage(out m_msg, IntPtr.Zero, 0, 0, 0) == 0;
        }

        private long _lastIdleRender = 0;
        private long _frameToFrameMinimum = 0;

        [System.Security.SuppressUnmanagedCodeSecurity]
        [DllImport("User32.dll", CharSet = CharSet.Unicode)]
        private static extern int PeekMessage(out Message msg, IntPtr hWnd, uint messageFilterMin, uint messageFilterMax, uint flags);
        private Message m_msg;

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
            return foreWindowProcess == System.Diagnostics.Process.GetCurrentProcess().Id;
        }
    }
}
