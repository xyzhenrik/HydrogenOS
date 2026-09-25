// SPDX-License-Identifier: GPL-3.0-or-later

use serde::{Deserialize, Serialize};
use std::fmt::{self, Display};
use std::fs;
use std::path::{Path, PathBuf};
use std::str::FromStr;
use std::sync::RwLock;
use thiserror::Error;

pub const BUS_NAME: &str = "org.hydrogen.Settings";
pub const INTERFACE_NAME: &str = "org.hydrogen.Settings1";
pub const OBJECT_PATH: &str = "/org/hydrogen/Settings1";
pub const CURRENT_SCHEMA_VERSION: u32 = 1;

#[derive(Clone, Copy, Debug, Default, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum MaterialQuality {
    #[default]
    Full,
    Efficient,
    Opaque,
}

impl Display for MaterialQuality {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Full => formatter.write_str("full"),
            Self::Efficient => formatter.write_str("efficient"),
            Self::Opaque => formatter.write_str("opaque"),
        }
    }
}

impl FromStr for MaterialQuality {
    type Err = SettingsError;

    fn from_str(value: &str) -> Result<Self, Self::Err> {
        match value {
            "full" => Ok(Self::Full),
            "efficient" => Ok(Self::Efficient),
            "opaque" => Ok(Self::Opaque),
            _ => Err(SettingsError::InvalidMaterialQuality(value.to_owned())),
        }
    }
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(default)]
pub struct Settings {
    pub schema_version: u32,
    pub material_quality: MaterialQuality,
    pub reduce_motion: bool,
    pub reduce_transparency: bool,
    pub diagnostics_opt_in: bool,
}

impl Default for Settings {
    fn default() -> Self {
        Self {
            schema_version: CURRENT_SCHEMA_VERSION,
            material_quality: MaterialQuality::Full,
            reduce_motion: false,
            reduce_transparency: false,
            diagnostics_opt_in: false,
        }
    }
}

#[derive(Debug, Error)]
pub enum SettingsError {
    #[error("unsupported settings schema {found}; newest supported schema is {supported}")]
    UnsupportedSchema { found: u32, supported: u32 },
    #[error("unknown material quality: {0}")]
    InvalidMaterialQuality(String),
    #[error("could not read or write settings: {0}")]
    Io(#[from] std::io::Error),
    #[error("could not decode settings: {0}")]
    Json(#[from] serde_json::Error),
    #[error("settings lock is poisoned")]
    Poisoned,
}

#[derive(Debug)]
pub struct SettingsStore {
    path: PathBuf,
    state: RwLock<Settings>,
}

impl SettingsStore {
    pub fn load(path: impl Into<PathBuf>) -> Result<Self, SettingsError> {
        let path = path.into();
        let settings = if path.exists() {
            let bytes = fs::read(&path)?;
            let settings: Settings = serde_json::from_slice(&bytes)?;
            if settings.schema_version > CURRENT_SCHEMA_VERSION {
                return Err(SettingsError::UnsupportedSchema {
                    found: settings.schema_version,
                    supported: CURRENT_SCHEMA_VERSION,
                });
            }
            Settings {
                schema_version: CURRENT_SCHEMA_VERSION,
                ..settings
            }
        } else {
            Settings::default()
        };

        Ok(Self {
            path,
            state: RwLock::new(settings),
        })
    }

    pub fn snapshot(&self) -> Result<Settings, SettingsError> {
        self.state
            .read()
            .map(|settings| settings.clone())
            .map_err(|_| SettingsError::Poisoned)
    }

    pub fn update(&self, operation: impl FnOnce(&mut Settings)) -> Result<Settings, SettingsError> {
        let mut state = self.state.write().map_err(|_| SettingsError::Poisoned)?;
        operation(&mut state);
        state.schema_version = CURRENT_SCHEMA_VERSION;
        save_atomically(&self.path, &state)?;
        Ok(state.clone())
    }
}

fn save_atomically(path: &Path, settings: &Settings) -> Result<(), SettingsError> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }

    let temporary_path = path.with_extension("json.tmp");
    let contents = serde_json::to_vec_pretty(settings)?;
    fs::write(&temporary_path, contents)?;
    fs::rename(&temporary_path, path)?;
    Ok(())
}

pub struct SettingsService {
    store: SettingsStore,
}

impl SettingsService {
    pub fn new(store: SettingsStore) -> Self {
        Self { store }
    }

    fn dbus_error(error: SettingsError) -> zbus::fdo::Error {
        zbus::fdo::Error::Failed(error.to_string())
    }
}

#[zbus::interface(name = "org.hydrogen.Settings1")]
impl SettingsService {
    fn schema_version(&self) -> zbus::fdo::Result<u32> {
        Ok(self
            .store
            .snapshot()
            .map_err(Self::dbus_error)?
            .schema_version)
    }

    fn material_quality(&self) -> zbus::fdo::Result<String> {
        Ok(self
            .store
            .snapshot()
            .map_err(Self::dbus_error)?
            .material_quality
            .to_string())
    }

    fn set_material_quality(&self, quality: &str) -> zbus::fdo::Result<()> {
        let quality = MaterialQuality::from_str(quality).map_err(Self::dbus_error)?;
        self.store
            .update(|settings| settings.material_quality = quality)
            .map_err(Self::dbus_error)?;
        Ok(())
    }

    fn reduce_motion(&self) -> zbus::fdo::Result<bool> {
        Ok(self
            .store
            .snapshot()
            .map_err(Self::dbus_error)?
            .reduce_motion)
    }

    fn set_reduce_motion(&self, enabled: bool) -> zbus::fdo::Result<()> {
        self.store
            .update(|settings| settings.reduce_motion = enabled)
            .map_err(Self::dbus_error)?;
        Ok(())
    }

    fn reduce_transparency(&self) -> zbus::fdo::Result<bool> {
        Ok(self
            .store
            .snapshot()
            .map_err(Self::dbus_error)?
            .reduce_transparency)
    }

    fn set_reduce_transparency(&self, enabled: bool) -> zbus::fdo::Result<()> {
        self.store
            .update(|settings| settings.reduce_transparency = enabled)
            .map_err(Self::dbus_error)?;
        Ok(())
    }

    fn diagnostics_opt_in(&self) -> zbus::fdo::Result<bool> {
        Ok(self
            .store
            .snapshot()
            .map_err(Self::dbus_error)?
            .diagnostics_opt_in)
    }

    fn set_diagnostics_opt_in(&self, enabled: bool) -> zbus::fdo::Result<()> {
        self.store
            .update(|settings| settings.diagnostics_opt_in = enabled)
            .map_err(Self::dbus_error)?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::tempdir;

    #[test]
    fn defaults_keep_diagnostics_disabled() {
        let temporary = tempdir().expect("temporary directory");
        let store =
            SettingsStore::load(temporary.path().join("settings.json")).expect("default settings");

        let settings = store.snapshot().expect("settings snapshot");
        assert_eq!(settings.schema_version, CURRENT_SCHEMA_VERSION);
        assert!(!settings.diagnostics_opt_in);
        assert!(!settings.reduce_motion);
        assert!(!settings.reduce_transparency);
    }

    #[test]
    fn missing_fields_migrate_to_safe_defaults() {
        let temporary = tempdir().expect("temporary directory");
        let path = temporary.path().join("settings.json");
        fs::write(&path, br#"{"schema_version":1,"reduce_motion":true}"#)
            .expect("legacy settings fixture");

        let settings = SettingsStore::load(path)
            .expect("migrated settings")
            .snapshot()
            .expect("settings snapshot");
        assert!(settings.reduce_motion);
        assert!(!settings.diagnostics_opt_in);
        assert_eq!(settings.material_quality, MaterialQuality::Full);
    }

    #[test]
    fn updates_persist_atomically() {
        let temporary = tempdir().expect("temporary directory");
        let path = temporary.path().join("settings.json");
        let store = SettingsStore::load(&path).expect("default settings");
        store
            .update(|settings| settings.material_quality = MaterialQuality::Efficient)
            .expect("persist update");

        let reloaded = SettingsStore::load(path)
            .expect("reloaded settings")
            .snapshot()
            .expect("settings snapshot");
        assert_eq!(reloaded.material_quality, MaterialQuality::Efficient);
    }

    #[test]
    fn future_schema_is_rejected() {
        let temporary = tempdir().expect("temporary directory");
        let path = temporary.path().join("settings.json");
        fs::write(&path, br#"{"schema_version":99}"#).expect("future fixture");

        assert!(matches!(
            SettingsStore::load(path),
            Err(SettingsError::UnsupportedSchema { found: 99, .. })
        ));
    }
}
