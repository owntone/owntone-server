using System;
using System.Drawing;
using System.Windows.Forms;
using System.Runtime.InteropServices;
using System.ComponentModel;
using System.Reflection;

namespace JCMLib
{
	public class NotifyIconEx : System.ComponentModel.Component
	{
		#region Notify Icon Target Window
		private class NotifyIconTarget : System.Windows.Forms.Form
		{
			public NotifyIconTarget()
			{
				this.Text = "Hidden NotifyIconTarget Window";
			}

			protected override void DefWndProc(ref Message msg)
			{
				if(msg.Msg == 0x400) // WM_USER
				{
					uint msgId = (uint)msg.LParam;
					uint id = (uint)msg.WParam;

					switch(msgId)
					{
						case 0x201: // WM_LBUTTONDOWN
							break;

						case 0x202: // WM_LBUTTONUP
							if(ClickNotify != null)
								ClickNotify(this, id);
							break;

						case 0x203: // WM_LBUTTONDBLCLK
							if(DoubleClickNotify != null)
								DoubleClickNotify(this, id);
							break;

						case 0x205: // WM_RBUTTONUP
							if(RightClickNotify != null)
								RightClickNotify(this, id);
							break;

						case 0x200: // WM_MOUSEMOVE
							break;

						case 0x402: // NIN_BALLOONSHOW
							break;

						// this should happen when the balloon is closed using the x
						// - we never seem to get this message!
						case 0x403: // NIN_BALLOONHIDE
							break;

						// we seem to get this next message whether the balloon times
						// out or whether it is closed using the x
						case 0x404: // NIN_BALLOONTIMEOUT
							break;

						case 0x405: // NIN_BALLOONUSERCLICK
							if(ClickBalloonNotify != null)
								ClickBalloonNotify(this, id);
							break;
					}
				}
				else if(msg.Msg == 0xC086) // WM_TASKBAR_CREATED
				{
					if(TaskbarCreated != null)
						TaskbarCreated(this, System.EventArgs.Empty);
				}
				else
				{
					base.DefWndProc(ref msg);
				}
			}

			public delegate void NotifyIconHandler(object sender, uint id);
		
			public event NotifyIconHandler ClickNotify;
			public event NotifyIconHandler DoubleClickNotify;
			public event NotifyIconHandler RightClickNotify;
			public event NotifyIconHandler ClickBalloonNotify;
			public event EventHandler TaskbarCreated;
		}
		#endregion

		#region Platform Invoke
		[StructLayout(LayoutKind.Sequential)] private struct NotifyIconData
		{
			public System.UInt32 cbSize; // DWORD
			public System.IntPtr hWnd; // HWND
			public System.UInt32 uID; // UINT
			public NotifyFlags uFlags; // UINT
			public System.UInt32 uCallbackMessage; // UINT
			public System.IntPtr hIcon; // HICON
			[MarshalAs(UnmanagedType.ByValTStr, SizeConst=128)]
			public System.String szTip; // char[128]
			public NotifyState dwState; // DWORD
			public NotifyState dwStateMask; // DWORD
			[MarshalAs(UnmanagedType.ByValTStr, SizeConst=256)]
			public System.String szInfo; // char[256]
			public System.UInt32 uTimeoutOrVersion; // UINT
			[MarshalAs(UnmanagedType.ByValTStr, SizeConst=64)]
			public System.String szInfoTitle; // char[64]
			public NotifyInfoFlags dwInfoFlags; // DWORD
		}

		[DllImport("shell32.Dll")]
		private static extern System.Int32 Shell_NotifyIcon(NotifyCommand cmd, ref NotifyIconData data);

		[DllImport("User32.Dll")]
		private static extern System.Int32 TrackPopupMenuEx(System.IntPtr hMenu,
			System.UInt32 uFlags,
			System.Int32 x,
			System.Int32 y,
			System.IntPtr hWnd,
			System.IntPtr ignore);

		[StructLayout(LayoutKind.Sequential)] private struct POINT
		{
			public System.Int32 x;
			public System.Int32 y;
		}

		[DllImport("User32.Dll")]
		private static extern System.Int32 GetCursorPos(ref POINT point);

		[DllImport("User32.Dll")]
		private static extern System.Int32 SetForegroundWindow(System.IntPtr hWnd);
		#endregion

		public enum NotifyInfoFlags {Error=0x03, Info=0x01, None=0x00, Warning=0x02}
		private enum NotifyCommand {Add=0x00, Delete=0x02, Modify=0x01}
		private enum NotifyFlags {Message=0x01, Icon=0x02, Tip=0x04, Info=0x10, State=0x08}
		private enum NotifyState {Hidden=0x01}

		private uint m_id = 0; // each icon in the notification area has an id
		private IntPtr m_handle; // save the handle so that we can remove icon
		private static NotifyIconTarget m_messageSink = new NotifyIconTarget();
		private static uint m_nextId = 1;
		private string m_text = "";
		private Icon m_icon = null;
		private ContextMenu m_contextMenu = null;
		private bool m_visible = false;
		private bool m_doubleClick = false; // fix for extra mouse up message we want to discard

		public event EventHandler Click;
		public event EventHandler DoubleClick;
		public event EventHandler BalloonClick;

		#region Properties
		public string Text
		{
			set
			{
				if(m_text != value)
				{
					m_text = value;
					CreateOrUpdate();
				}
			}
			get
			{
				return m_text;
			}
		}

		public Icon Icon
		{
			set
			{
				m_icon = value;
				CreateOrUpdate();
			}
			get
			{
				return m_icon;
			}
		}

		public ContextMenu ContextMenu
		{
			set
			{
				m_contextMenu = value;
			}
			get
			{
				return m_contextMenu;
			}
		}

		public bool Visible
		{
			set
			{
				if(m_visible != value)
				{
					m_visible = value;
					CreateOrUpdate();
				}
			}
			get
			{
				return m_visible;
			}
		}
		#endregion

		public NotifyIconEx()
		{
		}

		// this method adds the notification icon if it has not been added and if we
		// have enough data to do so
		private void CreateOrUpdate()
		{
			if(this.DesignMode)
				return;

			if(m_id == 0)
			{
				if(m_icon != null)
				{
					// create icon using available properties
					Create(m_nextId++);
				}
			}
			else
			{
				// update notify icon
				Update();
			}
		}

		private void Create(uint id)
		{
			NotifyIconData data = new NotifyIconData();
			data.cbSize = (uint)Marshal.SizeOf(data);

			m_handle = m_messageSink.Handle;
			data.hWnd = m_handle;
			m_id = id;
			data.uID = m_id;

			data.uCallbackMessage = 0x400;
			data.uFlags |= NotifyFlags.Message;

			data.hIcon = m_icon.Handle; // this should always be valid
			data.uFlags |= NotifyFlags.Icon;

			data.szTip = m_text;
			data.uFlags |= NotifyFlags.Tip;

			if(!m_visible)
				data.dwState = NotifyState.Hidden;
			data.dwStateMask |= NotifyState.Hidden;

			Shell_NotifyIcon(NotifyCommand.Add, ref data);

			// add handlers
			m_messageSink.ClickNotify += new NotifyIconTarget.NotifyIconHandler(OnClick);
			m_messageSink.DoubleClickNotify += new NotifyIconTarget.NotifyIconHandler(OnDoubleClick);
			m_messageSink.RightClickNotify += new NotifyIconTarget.NotifyIconHandler(OnRightClick);
			m_messageSink.ClickBalloonNotify += new NotifyIconTarget.NotifyIconHandler(OnClickBalloon);
			m_messageSink.TaskbarCreated += new EventHandler(OnTaskbarCreated);
		}

		// update an existing icon
		private void Update()
		{
			NotifyIconData data = new NotifyIconData();
			data.cbSize = (uint)Marshal.SizeOf(data);

			data.hWnd = m_messageSink.Handle;
			data.uID = m_id;

			data.hIcon = m_icon.Handle; // this should always be valid
			data.uFlags |= NotifyFlags.Icon;

			data.szTip = m_text;
			data.uFlags |= NotifyFlags.Tip;
			data.uFlags |= NotifyFlags.State;

			if(!m_visible)
				data.dwState = NotifyState.Hidden;
			data.dwStateMask |= NotifyState.Hidden;

			Shell_NotifyIcon(NotifyCommand.Modify, ref data);
		}

		protected override void Dispose(bool disposing)
		{
			Remove();
			base.Dispose(disposing);
		}

		public void Remove()
		{
			if(m_id != 0)
			{
				// remove the notify icon
				NotifyIconData data = new NotifyIconData();
				data.cbSize = (uint)Marshal.SizeOf(data);
					
				data.hWnd = m_handle;
				data.uID = m_id;

				Shell_NotifyIcon(NotifyCommand.Delete, ref data);

				m_id = 0;
			}
		}

		public void ShowBalloon(string title, string text, NotifyInfoFlags type, int timeoutInMilliSeconds)
		{
			if(timeoutInMilliSeconds < 0)
				throw new ArgumentException("The parameter must be positive", "timeoutInMilliseconds");

			NotifyIconData data = new NotifyIconData();
			data.cbSize = (uint)Marshal.SizeOf(data);
				
			data.hWnd = m_messageSink.Handle;
			data.uID = m_id;

			data.uFlags = NotifyFlags.Info;
			data.uTimeoutOrVersion = (uint)timeoutInMilliSeconds; // this value does not seem to work - any ideas?
			data.szInfoTitle = title;
			data.szInfo = text;
			data.dwInfoFlags = type;

			Shell_NotifyIcon(NotifyCommand.Modify, ref data);
		}

		#region Message Handlers

		private void OnClick(object sender, uint id)
		{
			if(id == m_id)
			{
				if(!m_doubleClick && Click != null)
					Click(this, EventArgs.Empty);
				m_doubleClick = false;
			}
		}

		private void OnRightClick(object sender, uint id)
		{
			if(id == m_id)
			{
				// show context menu
				if(m_contextMenu != null)
				{					
					POINT point = new POINT();
					GetCursorPos(ref point);
					
					SetForegroundWindow(m_messageSink.Handle); // this ensures that if we show the menu and then click on another window the menu will close

					// call non public member of ContextMenu
					m_contextMenu.GetType().InvokeMember("OnPopup",
						BindingFlags.NonPublic|BindingFlags.InvokeMethod|BindingFlags.Instance,
						null, m_contextMenu, new Object[] {System.EventArgs.Empty});

					TrackPopupMenuEx(m_contextMenu.Handle, 64, point.x, point.y, m_messageSink.Handle, IntPtr.Zero);
					
					// PostMessage(m_messageSink.Handle, 0, IntPtr.Zero, IntPtr.Zero);
				}
			}
		}

		private void OnDoubleClick(object sender, uint id)
		{
			if(id == m_id)
			{
				m_doubleClick = true;
				if(DoubleClick != null)
					DoubleClick(this, EventArgs.Empty);
			}
		}

		private void OnClickBalloon(object sender, uint id)
		{
			if(id == m_id)
				if(BalloonClick != null)
					BalloonClick(this, EventArgs.Empty);
		}

		private void OnTaskbarCreated(object sender, EventArgs e)
		{
			if(m_id != 0)
				Create(m_id); // keep the id the same
		}
		#endregion
	}
}
