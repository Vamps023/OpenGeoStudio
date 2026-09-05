import { useStore } from '../src/state/store'
import { serializeProject, deserializeProject } from '../src/domain/project'
import type { Project } from '../src/domain/project'

let failures = 0
function check(name: string, condition: boolean) {
  if (!condition) failures++
  console.log(condition ? 'ok  ' : 'FAIL', name)
}

Object.defineProperty(globalThis, 'window', { value: {}, configurable: true })
const initialState = useStore.getState()
const project: Project = { id: 'configuration-test', name: 'Configuration test', createdAt: '2026-09-05', roads: [], suppressedJunctions: [] }
useStore.setState({ projects: [project], activeProjectId: project.id, hydrated: false, history: { past: [], future: [] } })
const action = useStore.getState().setJunctionConfiguration
check('junction configuration has a store mutation', typeof action === 'function')
if (typeof action === 'function') {
  const connections = [{ fromRoadId: 'a', fromContact: 'end' as const, fromLaneId: -1, toRoadId: 'b', toContact: 'start' as const, toLaneId: -1, enabled: true }]
  action('a|b:0', { name: 'Central intersection', connections })
  connections[0].enabled = false
  let current = useStore.getState().projects[0]
  check('junction settings are stored on the active project', current.junctionConfigurations?.['a|b:0'].name === 'Central intersection')
  check('junction configuration does not retain mutable caller rows', current.junctionConfigurations?.['a|b:0'].connections?.[0].enabled === true)
  check('junction settings preserve previous undo snapshot', project.junctionConfigurations === undefined)
  action('a|b:0', { markings: false })
  current = useStore.getState().projects[0]
  check('configuration patches preserve existing connections', current.junctionConfigurations?.['a|b:0'].connections?.length === 1)
  check('configuration changes persist through serialization', deserializeProject(JSON.parse(JSON.stringify(serializeProject(current))))?.junctionConfigurations?.['a|b:0'].markings === false)
  useStore.getState().undo()
  check('undo restores prior junction configuration', useStore.getState().projects[0].junctionConfigurations?.['a|b:0'].markings === undefined)
  useStore.getState().redo()
  check('redo reapplies junction configuration', useStore.getState().projects[0].junctionConfigurations?.['a|b:0'].markings === false)
  action('a|b:0', { connections: [] })
  check('empty connection list is retained as all movements closed', useStore.getState().projects[0].junctionConfigurations?.['a|b:0'].connections?.length === 0)
  action('a|b:0', { connections: undefined })
  current = useStore.getState().projects[0]
  check('reset to automatic connections preserves name and markings', current.junctionConfigurations?.['a|b:0'].connections === undefined && current.junctionConfigurations?.['a|b:0'].name === 'Central intersection' && current.junctionConfigurations?.['a|b:0'].markings === false)
}
useStore.setState(initialState)
console.log(failures === 0 ? '\nALL PASSED' : `\n${failures} FAILURES`)
process.exitCode = failures === 0 ? 0 : 1
