const { spawn } = require('node:child_process')
const electronPath = require('electron')

delete process.env.ELECTRON_RUN_AS_NODE

const child = spawn(electronPath, ['.'], {
  cwd: process.cwd(),
  env: process.env,
  stdio: 'inherit',
})

child.on('exit', (code) => process.exit(code ?? 0))
child.on('error', (error) => {
  console.error(error.message)
  process.exit(1)
})
