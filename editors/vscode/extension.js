"use strict";

// VS Code client for the Gamma language server (`gamma lsp`, standard LSP over stdio).
// Syntax highlighting comes from the TextMate grammar and works without this file.

const vscode = require("vscode");
const { LanguageClient, TransportKind } = require("vscode-languageclient/node");

/** @type {LanguageClient | undefined} */
let client;
/** @type {vscode.OutputChannel | undefined} */
let output;

function settings() {
  const config = vscode.workspace.getConfiguration("gamma");
  return {
    path: (config.get("path") || "gamma").trim() || "gamma",
    enable: config.get("lsp.enable", true),
  };
}

async function startClient(context) {
  const { path, enable } = settings();
  if (!enable) {
    return;
  }
  output = output || vscode.window.createOutputChannel("Gamma Language Server");

  const serverOptions = {
    command: path,
    args: ["lsp"],
    transport: TransportKind.stdio,
  };
  const clientOptions = {
    documentSelector: [{ scheme: "file", language: "gamma" }, { scheme: "untitled", language: "gamma" }],
    outputChannel: output,
  };

  const next = new LanguageClient("gamma", "Gamma Language Server", serverOptions, clientOptions);
  try {
    await next.start();
    client = next;
  } catch (err) {
    client = undefined;
    reportStartFailure(path, err);
  }
}

function reportStartFailure(path, err) {
  const message = err && err.message ? err.message : String(err);
  const notFound = /ENOENT|not found|spawn/i.test(message);
  const text = notFound
    ? `Could not run \`${path}\`. Install the Gamma compiler and make sure \`gamma\` is on PATH, ` +
      "or set \"gamma.path\" to the executable. Highlighting still works without the language server."
    : `The Gamma language server failed to start: ${message}`;
  vscode.window.showErrorMessage(text, "Open Settings", "Disable Language Server").then((choice) => {
    if (choice === "Open Settings") {
      vscode.commands.executeCommand("workbench.action.openSettings", "gamma.path");
    } else if (choice === "Disable Language Server") {
      vscode.workspace.getConfiguration("gamma").update("lsp.enable", false, vscode.ConfigurationTarget.Global);
    }
  });
}

async function stopClient() {
  const running = client;
  client = undefined;
  if (running) {
    try {
      await running.stop();
    } catch (_) {
      // The server may already have exited.
    }
  }
}

async function restart(context) {
  await stopClient();
  await startClient(context);
}

function activate(context) {
  context.subscriptions.push(
    vscode.commands.registerCommand("gamma.restartServer", () => restart(context)),
    vscode.workspace.onDidChangeConfiguration((e) => {
      if (e.affectsConfiguration("gamma.path") || e.affectsConfiguration("gamma.lsp.enable")) {
        restart(context);
      }
    }),
  );
  return startClient(context);
}

function deactivate() {
  return stopClient();
}

module.exports = { activate, deactivate };
