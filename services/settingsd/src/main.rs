// SPDX-License-Identifier: GPL-3.0-or-later

use anyhow::{Context, Result};
use hydrogen_settingsd::{BUS_NAME, OBJECT_PATH, SettingsService, SettingsStore};
use std::env;
use std::path::PathBuf;

fn settings_path() -> Result<PathBuf> {
    if let Some(config_home) = env::var_os("XDG_CONFIG_HOME") {
        return Ok(PathBuf::from(config_home)
            .join("hydrogen")
            .join("settings-v1.json"));
    }

    let home = env::var_os("HOME").context("HOME is not set")?;
    Ok(PathBuf::from(home)
        .join(".config")
        .join("hydrogen")
        .join("settings-v1.json"))
}

#[tokio::main]
async fn main() -> Result<()> {
    let store = SettingsStore::load(settings_path()?)?;
    let service = SettingsService::new(store);

    let _connection = zbus::connection::Builder::session()?
        .name(BUS_NAME)?
        .serve_at(OBJECT_PATH, service)?
        .build()
        .await?;

    tokio::signal::ctrl_c().await?;
    Ok(())
}
