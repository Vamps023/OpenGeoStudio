import { BrowserWindow } from 'electron';
import * as path from 'path';
import * as fs from 'fs/promises';

export async function createMainWindow(): Promise<BrowserWindow> {
  const isDev = !!process.env.VITE_DEV_SERVER_URL;

  const win = new BrowserWindow({
    width: 1600,
    height: 1000,
    minWidth: 1200,
    minHeight: 800,
    title: 'OpenGeoStudio',
    icon: path.join(__dirname, '../../../assets/logo/logo.png'),
    darkTheme: true,
    backgroundColor: '#1a1a1a',
    autoHideMenuBar: true,
    webPreferences: {
      preload: path.join(__dirname, '../preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: false,
      webSecurity: true,
      allowRunningInsecureContent: false,
    },
    show: false,
  });

  const devServerUrl = process.env.VITE_DEV_SERVER_URL || 'http://localhost:5173';
  const distPath = path.join(__dirname, '../../../dist/index.html');

  let distExists = false;
  try {
    await fs.access(distPath);
    distExists = true;
  } catch {
    distExists = false;
  }

  if (isDev || !distExists) {
    win.loadURL(devServerUrl);
    win.webContents.openDevTools();
  } else {
    win.loadFile(distPath);
  }

  win.once('ready-to-show', () => {
    win.show();
  });

  return win;
}
