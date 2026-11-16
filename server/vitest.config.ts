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
                        // Real limits in tests (the dev environment multiplies them).
                        RATE_LIMIT_SCALE: "1",
                        // Faster hashing in tests; production uses 100 000 iterations.
                        PBKDF2_ITERATIONS: "10000",
                        // Backups: no wait before the pre_import copy; 1 MiB parts (several per file).
                        BACKUP_SETTLE_MS: "0",
                        BACKUP_IMPORT_PART_SIZE: "1048576",
                    },
                },
            }),
        ],
        test: { setupFiles: ["./test/setup.ts"] },
    };
});
