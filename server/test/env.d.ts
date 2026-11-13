// Test-only bindings declared in vitest.config.ts.
declare namespace Cloudflare {
    interface Env {
        TEST_MIGRATIONS: import("cloudflare:test").D1Migration[];
        ADMIN_TOKEN: string;
    }
}
