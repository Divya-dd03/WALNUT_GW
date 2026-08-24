/**
 * @file config.c
 * @brief Configuration management implementation
 */

/*---------------------------------------------------------------
 * Standard Includes
 *--------------------------------------------------------------*/
#include <string.h>

/*---------------------------------------------------------------
 * Weware Platform Includes
 *--------------------------------------------------------------*/
#include "config/config.h"
#include "weware_version.h"
#include "system/storage/flash_paths.h"
#include "common/utils.h"
#include "module/module_manager.h"

/*---------------------------------------------------------------
 * SDK Platform Abstraction Layer
 *--------------------------------------------------------------*/
#include "sdk_platform.h"
#include "functionality/sdk_functionality_file.h"

/*---------------------------------------------------------------
 * Log Configuration
 *--------------------------------------------------------------*/
#define LOG_TAG "CONFIG"
#define LOG_MODULE_LEVEL LOG_LEVEL_ERROR
#include "module/log/log.h"

/*---------------------------------------------------------------
 * External Declarations
 *--------------------------------------------------------------*/
extern Module *g_modules[];
extern const UINT32 g_module_count;

/*---------------------------------------------------------------
 * Configuration Constants
 *--------------------------------------------------------------*/
#define CONFIG_FILE_MAGIC 0x57454346  /* "WECF" - WEWare Config File */
#define MAX_LOAD_RETRIES 3
#define CONFIG_LOAD_RETRY_DELAY_MS 100
#define MODULE_NAME_MAX_SIZE 32

/*---------------------------------------------------------------
 * File Format Definitions
 *--------------------------------------------------------------*/
/**
 * @brief Config file header structure
 */
typedef struct
{
    UINT32 magic;
    UINT32 version;
    UINT32 entry_count;
} ConfigFileHeader;

/*---------------------------------------------------------------
 * Global State
 *--------------------------------------------------------------*/
static BOOL g_config_initialized = FALSE;
static BOOL g_config_file_loaded = FALSE;

/*---------------------------------------------------------------
 * Static Function Prototypes
 *--------------------------------------------------------------*/
static Module *find_module_by_name(const char *name);
static size_t get_module_config_size(Module *module);
static void initialize_module_defaults(void);
static Result read_config_header(void *fp, ConfigFileHeader *header, const char *file_path);
static Result read_config_entry(void *fp, char *module_name, UINT32 *config_size);
static Result load_module_config(void *fp, Module *module, UINT32 file_config_size);
static UINT32 count_savable_modules(void);
static Result write_config_header(void *fp, UINT32 module_count);
static Result write_module_entry(void *fp, Module *module);

/*---------------------------------------------------------------
 * Internal Helper Functions
 *--------------------------------------------------------------*/

/**
 * @brief Find module by name (for backward compatibility with file-based config)
 */
static Module *find_module_by_name(const char *name)
{
    if (!name)
    {
        LOG_DEBUG("find_module_by_name: NULL name");
        return NULL;
    }
    
    for (UINT32 i = 0; i < g_module_count; i++)
    {
        if (g_modules[i] && g_modules[i]->config.name && 
            strcmp(g_modules[i]->config.name, name) == 0)
        {
            return g_modules[i];
        }
    }
    
    LOG_DEBUG("find_module_by_name: module '%s' not found", name);
    return NULL;
}

/**
 * @brief Get config size for a module
 */
static size_t get_module_config_size(Module *module)
{
    if (!module || !module->config.config_ptr)
    {
        return 0;
    }
    return module->config.config_size;
}

/*---------------------------------------------------------------
 * Module Configuration Management
 *--------------------------------------------------------------*/

/**
 * @brief Initialize all module configs with their defaults
 * @note This function only manages module configs, not device/system configs
 */
static void initialize_module_defaults(void)
{
    static BOOL defaults_initialized = FALSE;
    if (defaults_initialized)
    {
        return;
    }
    
    UINT32 initialized_count = 0;
    for (UINT32 i = 0; i < g_module_count; i++)
    {
        Module *module = g_modules[i];
        if (!module || !module->config.config_get_defaults_fn || !module->config.config_ptr)
        {
            continue;
        }
        
        typedef void (*GetDefaultsFn)(void *);
        GetDefaultsFn get_defaults_fn = (GetDefaultsFn)module->config.config_get_defaults_fn;
        get_defaults_fn(module->config.config_ptr);
        initialized_count++;
    }
    
    LOG_DEBUG("Initialized defaults for %u modules", initialized_count);
    defaults_initialized = TRUE;
}

/*---------------------------------------------------------------
 * File I/O Helper Functions
 *--------------------------------------------------------------*/

/**
 * @brief Read config file header with retry logic
 */
static Result read_config_header(void *fp, ConfigFileHeader *header, const char *file_path)
{
    if (!fp || !header || !file_path)
    {
        LOG_ERROR("read_config_header: invalid parameters");
        return RESULT_INVALID_PARAM;
    }
    
    int retry_count = 0;
    BOOL header_read_success = FALSE;

    while (retry_count < MAX_LOAD_RETRIES && !header_read_success)
    {
        UINT32 read_len = 0;
        if (sdk_file_read(fp, header, sizeof(ConfigFileHeader), &read_len) == SDK_RESULT_SUCCESS && 
            read_len == sizeof(ConfigFileHeader))
        {
            header_read_success = TRUE;
        }
        else
        {
            retry_count++;
            if (retry_count < MAX_LOAD_RETRIES)
            {
                LOG_DEBUG("Header read failed, retrying (%d/%d)", 
                         retry_count, MAX_LOAD_RETRIES);
                sdk_file_close(fp);
                utils_sleep_ms(CONFIG_LOAD_RETRY_DELAY_MS);
                fp = sdk_file_open(file_path, "rb");
                if (fp == NULL)
                {
                    LOG_WARN("Failed to reopen file for retry");
                    return RESULT_ERROR;
                }
            }
        }
    }
    
    if (!header_read_success)
    {
        LOG_WARN("Failed to read header after %d retries", MAX_LOAD_RETRIES);
        return RESULT_ERROR;
    }
    
    return RESULT_SUCCESS;
}

/**
 * @brief Read a single config entry header (module name and size)
 */
static Result read_config_entry(void *fp, char *module_name, UINT32 *config_size)
{
    if (!fp || !module_name || !config_size)
    {
        LOG_ERROR("read_config_entry: invalid parameters");
        return RESULT_INVALID_PARAM;
    }
    
    UINT32 read_len = 0;
    if (sdk_file_read(fp, module_name, MODULE_NAME_MAX_SIZE, &read_len) != SDK_RESULT_SUCCESS || 
        read_len != MODULE_NAME_MAX_SIZE)
    {
        LOG_DEBUG("Failed to read module name (read_len=%u)", read_len);
        return RESULT_ERROR;
    }
    
    if (sdk_file_read(fp, config_size, sizeof(UINT32), &read_len) != SDK_RESULT_SUCCESS ||
        read_len != sizeof(UINT32))
    {
        LOG_DEBUG("Failed to read config size (read_len=%u)", read_len);
        return RESULT_ERROR;
    }
    
    return RESULT_SUCCESS;
}

/**
 * @brief Load configuration for a single module from file
 */
static Result load_module_config(void *fp, Module *module, UINT32 file_config_size)
{
    if (!fp || !module || !module->config.config_ptr)
    {
        LOG_ERROR("load_module_config: invalid parameters");
        return RESULT_INVALID_PARAM;
    }

    size_t expected_size = get_module_config_size(module);
    
    /* Initialize with defaults before loading */
    if (module->config.config_get_defaults_fn)
    {
        typedef void (*GetDefaultsFn)(void *);
        GetDefaultsFn get_defaults_fn = (GetDefaultsFn)module->config.config_get_defaults_fn;
        get_defaults_fn(module->config.config_ptr);
    }
    
    /* Load only up to expected size (handle version mismatches) */
    size_t load_size = (file_config_size < expected_size) ? file_config_size : expected_size;
    
    UINT32 read_len = 0;
    if (sdk_file_read(fp, module->config.config_ptr, load_size, &read_len) != SDK_RESULT_SUCCESS ||
        read_len != load_size)
    {
        LOG_WARN("Failed to load config for '%s' (read_len=%u, expected=%zu)",
                 module->config.name, read_len, load_size);
        
        /* Skip remaining bytes if file version is newer */
        if (file_config_size > load_size)
        {
            sdk_file_seek(fp, file_config_size - load_size, 1);  /* SEEK_CUR = 1 */
        }
        return RESULT_ERROR;
    }

    /* Skip remaining bytes if file version is newer */
    if (file_config_size > load_size)
    {
        sdk_file_seek(fp, file_config_size - load_size, 1);  /* SEEK_CUR = 1 */
        LOG_DEBUG("Config '%s' version mismatch (file=%u, expected=%zu), skipped %u bytes",
                 module->config.name, file_config_size, expected_size, file_config_size - load_size);
    }
    
    /* Validate loaded config */
    if (module->config.config_validate_fn)
    {
        typedef Result (*ValidateFn)(const void *);
        ValidateFn validate_fn = (ValidateFn)module->config.config_validate_fn;
        if (validate_fn(module->config.config_ptr) != RESULT_SUCCESS)
        {
            LOG_WARN("Config '%s' validation failed, reverting to defaults",
                     module->config.name);
            
            /* Revert to defaults on validation failure */
            if (module->config.config_get_defaults_fn)
            {
                typedef void (*GetDefaultsFn)(void *);
                GetDefaultsFn get_defaults_fn = (GetDefaultsFn)module->config.config_get_defaults_fn;
                get_defaults_fn(module->config.config_ptr);
            }
            return RESULT_ERROR;
        }
    }
    
    LOG_DEBUG("Successfully loaded config for '%s'", module->config.name);
    return RESULT_SUCCESS;
}

/**
 * @brief Count modules that can be saved to file
 */
static UINT32 count_savable_modules(void)
{
    UINT32 count = 0;
    for (UINT32 i = 0; i < g_module_count; i++)
    {
        Module *module = g_modules[i];
        if (module && module->config.config_ptr && module->config.config_validate_fn && 
            module->config.config_stored)
        {
            typedef Result (*ValidateFn)(const void *);
            ValidateFn validate_fn = (ValidateFn)module->config.config_validate_fn;
            if (validate_fn(module->config.config_ptr) == RESULT_SUCCESS)
            {
                count++;
            }
        }
    }
    return count;
}

/**
 * @brief Write config file header
 */
static Result write_config_header(void *fp, UINT32 module_count)
{
    if (!fp)
    {
        LOG_ERROR("write_config_header: NULL file pointer");
        return RESULT_INVALID_PARAM;
    }
    
    ConfigFileHeader header = {
        .magic = CONFIG_FILE_MAGIC,
        .version = CONFIG_FILE_VERSION,
        .entry_count = module_count
    };

    UINT32 written = 0;
    if (sdk_file_write(fp, &header, sizeof(ConfigFileHeader), &written) != SDK_RESULT_SUCCESS ||
        written != sizeof(ConfigFileHeader))
    {
        LOG_ERROR("Failed to write header (written=%u, expected=%zu)",
                 written, sizeof(ConfigFileHeader));
        return RESULT_ERROR;
    }
    
    return RESULT_SUCCESS;
}

/**
 * @brief Write a single module entry to file
 */
static Result write_module_entry(void *fp, Module *module)
{
    if (!fp || !module || !module->config.config_ptr || !module->config.name)
    {
        LOG_ERROR("write_module_entry: invalid parameters");
        return RESULT_INVALID_PARAM;
    }
    
    size_t config_size = get_module_config_size(module);
    if (config_size == 0)
    {
        LOG_DEBUG("Skipping '%s' - zero config size", module->config.name);
        return RESULT_SUCCESS;
    }
    
    char module_name[MODULE_NAME_MAX_SIZE] = {0};
    if (utils_strncpy_safe(module_name, module->config.name, sizeof(module_name)) < 0)
    {
        LOG_ERROR("Module name '%s' too long", module->config.name);
        return RESULT_ERROR;
    }
    
    UINT32 config_size_uint32 = (UINT32)config_size;
    UINT32 written = 0;
    
    /* Write module name */
    if (sdk_file_write(fp, module_name, sizeof(module_name), &written) != SDK_RESULT_SUCCESS ||
        written != sizeof(module_name))
    {
        LOG_ERROR("Failed to write module name for '%s'", module->config.name);
        return RESULT_ERROR;
    }
    
    /* Write config size */
    if (sdk_file_write(fp, &config_size_uint32, sizeof(config_size_uint32), &written) != SDK_RESULT_SUCCESS ||
        written != sizeof(config_size_uint32))
    {
        LOG_ERROR("Failed to write config size for '%s'", module->config.name);
        return RESULT_ERROR;
    }
    
    /* Write config data */
    if (sdk_file_write(fp, module->config.config_ptr, config_size, &written) != SDK_RESULT_SUCCESS ||
        written != config_size)
    {
        LOG_ERROR("Failed to write config data for '%s' (written=%u, expected=%zu)",
                 module->config.name, written, config_size);
        return RESULT_ERROR;
    }
    
    LOG_DEBUG("Successfully wrote config for '%s' (%zu bytes)",
             module->config.name, config_size);
    return RESULT_SUCCESS;
}

/*---------------------------------------------------------------
 * Public Configuration API
 *--------------------------------------------------------------*/

/**
 * @brief Initialize module configurations with default values
 * @note This function only initializes module configs
 */
void config_init_defaults(void)
{
    /* Initialize module configs with defaults */
    initialize_module_defaults();
    
    LOG_INFO("Module configuration defaults initialized");
}

/*---------------------------------------------------------------
 * Configuration File I/O
 *--------------------------------------------------------------*/

Result config_load_from_file(const char *config_file_path)
{
    if (g_config_file_loaded)
    {
        LOG_DEBUG("Config file already loaded");
        return RESULT_SUCCESS;
    }
    
    /* Initialize with module defaults first */
    config_init_defaults();
    
    const char *file_path = config_file_path ? config_file_path : CONFIG_FILE_PATH;

    void *fp = sdk_file_open(file_path, "rb");

    if (fp == NULL)
    {
        LOG_INFO("Config file '%s' not found, creating with defaults", file_path);
        Result save_result = config_save_to_file(file_path);
        if (save_result != RESULT_SUCCESS)
        {
            LOG_WARN("Failed to create default config file");
        }
        g_config_file_loaded = TRUE;
        return RESULT_SUCCESS;
    }
    
    /* Read header with retry logic */
    ConfigFileHeader header;
    Result header_result = read_config_header(fp, &header, file_path);
    if (header_result != RESULT_SUCCESS)
    {
        LOG_WARN("Failed to read header, using defaults");
        sdk_file_close(fp);
        g_config_file_loaded = TRUE;
        return RESULT_SUCCESS;
    }
    
    /* Validate header */
    if (header.magic != CONFIG_FILE_MAGIC)
    {
        LOG_WARN("Invalid magic number (0x%08X), using defaults", header.magic);
        sdk_file_close(fp);
        g_config_file_loaded = TRUE;
        return RESULT_SUCCESS;
    }
    
    if (header.version != CONFIG_FILE_VERSION)
    {
        LOG_WARN("Version mismatch (file=%u, expected=%u), deleting and recreating with defaults",
                 header.version, CONFIG_FILE_VERSION);
        sdk_file_close(fp);
        if (sdk_file_delete(file_path) != SDK_RESULT_SUCCESS)
            LOG_WARN("Failed to delete outdated config file '%s'", file_path);
        Result save_result = config_save_to_file(file_path);
        if (save_result != RESULT_SUCCESS)
            LOG_WARN("Failed to create config file after version reset");
        else
            LOG_INFO("Config file recreated at version %u", CONFIG_FILE_VERSION);
        g_config_file_loaded = TRUE;
        return RESULT_SUCCESS;
    }
    
    LOG_INFO("Loading config from '%s' (%u entries)", file_path, header.entry_count);
    
    /* Read module config entries */
    BOOL any_config_updated = FALSE;
    BOOL file_corrupted = FALSE;
    UINT32 loaded_count = 0;
    UINT32 failed_count = 0;
    
    for (UINT32 entry_idx = 0; entry_idx < header.entry_count; entry_idx++)
    {
        char module_name[MODULE_NAME_MAX_SIZE];
        UINT32 config_size;
        
        if (read_config_entry(fp, module_name, &config_size) != RESULT_SUCCESS)
        {
            LOG_WARN("Failed to read entry %u, stopping", entry_idx);
            failed_count++;
            file_corrupted = TRUE;
            break;
        }
        
        Module *module = find_module_by_name(module_name);
        if (!module || !module->config.config_ptr || !module->config.config_validate_fn || 
            !module->config.config_stored)
        {
            LOG_DEBUG("Skipping '%s' - not stored or invalid", module_name);
            
            /* Skip config data */
            sdk_file_seek(fp, config_size, 1);  /* SEEK_CUR = 1 */
            continue;
        }
        
        Result load_result = load_module_config(fp, module, config_size);
        if (load_result == RESULT_SUCCESS)
        {
            loaded_count++;
        }
        else
        {
            failed_count++;
            if (config_size > get_module_config_size(module))
            {
                any_config_updated = TRUE;
            }
        }
    }
    
    sdk_file_close(fp);
    g_config_file_loaded = TRUE;
    
    LOG_INFO("Config loaded: %u succeeded, %u failed", loaded_count, failed_count);
    
    /* Save updated config for version-mismatch resize or file corruption/truncation recovery */
    if (any_config_updated || file_corrupted)
    {
        if (file_corrupted)
            LOG_WARN("Config file appears corrupted/truncated, rewriting with current defaults");
        else
            LOG_INFO("Config version updated, saving to file");
        Result save_result = config_save_to_file(file_path);
        if (save_result != RESULT_SUCCESS)
        {
            LOG_WARN("Failed to save updated config");
        }
    }
    
    return RESULT_SUCCESS;
}

Result config_save_to_file(const char *config_file_path)
{
    
    /* Count and validate modules */
    UINT32 module_count = count_savable_modules();
    if (module_count == 0)
    {
        LOG_WARN("No modules to save");
        return RESULT_SUCCESS;
    }
    
    const char *file_path = config_file_path ? config_file_path : CONFIG_FILE_PATH;
    
    void* fp = sdk_file_open(file_path, "wb");
    if (fp == NULL)
    {
        LOG_ERROR("Failed to open '%s' for writing", file_path);
        return RESULT_ERROR;
    }
    
    /* Write header */
    if (write_config_header(fp, module_count) != RESULT_SUCCESS)
    {
        sdk_file_close(fp);
        return RESULT_ERROR;
    }
    
    /* Write module entries */
    UINT32 saved_count = 0;
    for (UINT32 i = 0; i < g_module_count; i++)
    {
        Module *module = g_modules[i];
        if (!module || !module->config.config_ptr || !module->config.config_validate_fn || 
            !module->config.config_stored)
        {
            continue;
        }
        
        /* Validate before saving */
        typedef Result (*ValidateFn)(const void *);
        ValidateFn validate_fn = (ValidateFn)module->config.config_validate_fn;
        if (validate_fn(module->config.config_ptr) != RESULT_SUCCESS)
        {
            LOG_WARN("Skipping '%s' - validation failed", module->config.name);
            continue;
        }
        
        if (write_module_entry(fp, module) == RESULT_SUCCESS)
        {
            saved_count++;
        }
        else
        {
            LOG_ERROR("Failed to write entry for '%s'", module->config.name);
            sdk_file_close(fp);
            return RESULT_ERROR;
        }
    }
    
    sdk_file_sync(fp);
    sdk_file_close(fp);

    LOG_INFO("Successfully saved %u modules to '%s'", saved_count, file_path);
    return RESULT_SUCCESS;
}

/*---------------------------------------------------------------
 * Configuration Validation
 *--------------------------------------------------------------*/

Result config_validate(void)
{
    /* Validate all module configs */
    for (UINT32 i = 0; i < g_module_count; i++)
    {
        Module *module = g_modules[i];
        if (!module || !module->config.config_validate_fn || !module->config.config_ptr)
        {
            continue;
        }
        
        typedef Result (*ValidateFn)(const void *);
        ValidateFn validate_fn = (ValidateFn)module->config.config_validate_fn;
        if (validate_fn(module->config.config_ptr) != RESULT_SUCCESS)
        {
            LOG_ERROR("Module '%s' validation failed", 
                     module->config.name ? module->config.name : "UNKNOWN");
            return RESULT_INVALID_PARAM;
        }
    }
    
    LOG_DEBUG("Configuration validation successful");
    return RESULT_SUCCESS;
}

/*---------------------------------------------------------------
 * Module Configuration Access
 *--------------------------------------------------------------*/

const void *config_get_module_config(const char *module_name)
{
    if (!module_name)
    {
        LOG_DEBUG("config_get_module_config: NULL module_name");
        return NULL;
    }
    
    Module *module = find_module_by_name(module_name);
    if (module && module->config.config_ptr)
    {
        return module->config.config_ptr;
    }
    
    LOG_DEBUG("Module '%s' config not found", module_name);
    return NULL;
}

void *config_get_module_config_mutable(const char *module_name)
{
    if (!module_name)
    {
        LOG_DEBUG("config_get_module_config_mutable: NULL module_name");
        return NULL;
    }
    
    Module *module = find_module_by_name(module_name);
    if (module && module->config.config_ptr)
    {
        return module->config.config_ptr;
    }
    
    LOG_DEBUG("Module '%s' config not found", module_name);
    return NULL;
}

/*---------------------------------------------------------------
 * Configuration Getter Functions
 *--------------------------------------------------------------*/

void config_get_current(void)
{
    if (!g_config_initialized)
    {
        LOG_DEBUG("Initializing config on first access");
        config_load_from_file(NULL);
        g_config_initialized = TRUE;
    }
}
