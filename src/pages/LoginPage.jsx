import { useState } from 'react'
import { Lock, User } from 'lucide-react'

import LogoMark from '@/components/layout/LogoMark'
import { Button } from '@/components/ui/button'
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from '@/components/ui/card'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'

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
    <main className="relative grid min-h-screen place-items-center overflow-hidden bg-background p-6">
      {/* Ambient background */}
      <div
        aria-hidden="true"
        className="pointer-events-none absolute inset-0 bg-[radial-gradient(circle_at_50%_15%,rgb(74_222_128/10%),transparent_32rem),linear-gradient(160deg,var(--background)_0%,#0c1322_55%,var(--background)_100%)]"
      />
      <div
        aria-hidden="true"
        className="pointer-events-none absolute top-1/2 left-1/2 size-[46rem] -translate-x-1/2 -translate-y-1/2 rounded-full bg-primary/5 blur-3xl"
      />

      <Card className="relative w-full max-w-md border-border/80 bg-card/80 shadow-2xl backdrop-blur">
        <CardHeader className="items-center pt-8 text-center">
          <LogoMark className="mx-auto mb-2" />
          <p className="text-xs font-bold tracking-[0.2em] text-primary uppercase">
            Road network creation
          </p>
          <CardTitle className="mt-1 text-3xl tracking-tight">OpenGeoStudio</CardTitle>
          <CardDescription>Sign in to continue to your workspace.</CardDescription>
        </CardHeader>
        <CardContent className="pb-8">
          <form className="grid gap-4" onSubmit={handleSubmit}>
            <div className="grid gap-2">
              <Label htmlFor="login-id">Login ID</Label>
              <div className="relative">
                <User className="pointer-events-none absolute top-1/2 left-3 size-4 -translate-y-1/2 text-muted-foreground" />
                <Input
                  id="login-id"
                  type="text"
                  className="pl-9"
                  value={userId}
                  onChange={(event) => setUserId(event.target.value)}
                  autoComplete="username"
                  autoFocus
                />
              </div>
            </div>
            <div className="grid gap-2">
              <Label htmlFor="login-password">Password</Label>
              <div className="relative">
                <Lock className="pointer-events-none absolute top-1/2 left-3 size-4 -translate-y-1/2 text-muted-foreground" />
                <Input
                  id="login-password"
                  type="password"
                  className="pl-9"
                  value={password}
                  onChange={(event) => setPassword(event.target.value)}
                  autoComplete="current-password"
                />
              </div>
            </div>
            {error && (
              <p className="text-center text-sm font-medium text-destructive">{error}</p>
            )}
            <Button type="submit" className="mt-1 w-full">
              Sign In
            </Button>
          </form>
        </CardContent>
      </Card>
    </main>
  )
}
