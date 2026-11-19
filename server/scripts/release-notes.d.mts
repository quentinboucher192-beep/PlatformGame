// Types of release-notes.mjs (imported by the server tests).
export function englishNotes(notes: string, strings: Record<string, string>): string | null;
export function releasesOfMigrations(files: Record<string, string>): { version: string; notes: string }[];
export function sqlText(s: string): string;
