import { useState } from 'react'

const ADMIN_ID = 'Admin'
const ADMIN_PASSWORD = 'Admin'

export default function LoginPage({ onLogin }) {
  const [userId, setUserId] = useState('')
  const [password, setPassword] = useState('')
  const [error, setError] = useState('')

  function handleSubmit(event) {
    event.preventDefault()
    if (userId === ADMIN_ID && password === ADMIN_PASSWORD) {
      setError('')
      onLogin()
    } else {
      setError('Invalid ID or password.')
    }
  }

  return (
    <main className="launch-page">
      <section className="launch-card">
        <div className="logo-mark" aria-hidden="true">
          <span />
          <span />
          <span />
        </div>
        <p className="eyebrow">Road network creation</p>
        <h1>OpenGeoStudio</h1>
        <p className="subtitle">Sign in to continue.</p>

        <form className="login-form" onSubmit={handleSubmit}>
          <label>
            <span>Login ID</span>
            <input
              type="text"
              value={userId}
              onChange={(event) => setUserId(event.target.value)}
              autoComplete="username"
              autoFocus
            />
          </label>
          <label>
            <span>Password</span>
            <input
              type="password"
              value={password}
              onChange={(event) => setPassword(event.target.value)}
              autoComplete="current-password"
            />
          </label>
          {error && <p className="login-error">{error}</p>}
          <button type="submit">Sign In</button>
        </form>
      </section>
    </main>
  )
}
