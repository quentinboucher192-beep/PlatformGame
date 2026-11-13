import { cloudflareTest, readD1Migrations } from "@cloudflare/vitest-plugin";
import { defineConfig } from "vitest/config";

// Tests run inside the Workers runtime (Miniflare) with a local D1 database and R2 bucket:
// no Cloudflare account and no network are needed.
export default defineConfig(async () => {
    const migrations = await readD1Migrations("./migrations");
    return {
        plugins: [
            cloudflareTest({
                wrangler: { configPath: "./wrangler.jsonc", environment: "dev" },
                miniflare: {
                    bindings: {
                        TEST_MIGRATIONS: migrations,
                        IP_HASH_SALT: "test-only-salt",
                        ADMIN_TOKEN: "test-admin-token-0123456789abcdef0123456789",
                        EDGE_CACHE_SECONDS: "0",
                        // Faster hashing in tests; production uses 100 000 iterations.
                        PBKDF2_ITERATIONS: "10000",
                    },
                },
            }),
        ],
        test: { setupFiles: ["./test/setup.ts"] },
    };
});
