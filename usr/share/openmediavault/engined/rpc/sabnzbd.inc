<?php

/**
 * Copyright (C) 2010-2012 Ian Moore <imooreyahoo@gmail.com>
 * Copyright (C) 2013-2015 OpenMediaVault Plugin Developers.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
 //

namespace OMV\Engined\Rpc;

use DateTime;
use Exception;
use OMV\Config\Database;
use OMV\Engine\Notify\Dispatcher;
use OMV\Rpc\ServiceAbstract;
use OMV\Rpc\ServiceManager as RpcServiceManager;
use OMV\System\Process;
use SplFileObject;



class Sabnzbd extends ServiceAbstract
{
    private $sabnzbdSettingsDir = "/var/opt/sabnzbd/";
    private $sabnzbdSettingsFile = "/var/opt/sabnzbd/sabnzbd.ini";

    private $sabnzbd2SettingsDir = "/var/opt/sabnzbd2/";
    private $sabnzbd2SettingsFile = "/var/opt/sabnzbd2/config.ini";



    private $database;

    private $dataModelPath = 'conf.service.sabnzbd';

    private $dispatcher;

    private $eventMessagePath = 'org.openmediavault.conf.services.sabnzbd';

    public function __construct(Database $database = null, Dispatcher $dispatcher = null)
    {
        $this->database = $database ?: Database::getInstance();
        $this->dispatcher = $dispatcher ?: Dispatcher::getInstance();
    }

    public function getName()
    {
        return 'Sabnzbd';
    }

    public function initialize()
    {
        $this->registerMethod("getSettings");
        $this->registerMethod("setSettings");
        $this->registerMethod("doUpdateSAB");
        $this->registerMethod("doBackup");
        $this->registerMethod("doRestore");
    }

    public function getSettings($params, $context)
    {
        $this->validateMethodContext($context, ['role' => OMV_ROLE_ADMINISTRATOR]);

        $cmd = 'omv-mkconf sabnzbd getsettings';

        $object = $this->database->get($this->dataModelPath);

        $port = 8080;
        $sslIsEnabled = 0;

        // Get current port as set in Sabnzbd config file
        if (file_exists($this->sabnzbdSettingsFile)) {
            $config = parse_ini_file(
                $this->sabnzbdSettingsFile,
                true,
                INI_SCANNER_RAW
            );

            $sslIsEnabled = isset($config["misc"]["enable_https"]) ? $config["misc"]["enable_https"] : '0';

            if ($sslIsEnabled) {
                if (!empty($config["misc"]["https_port"])) {
                    $port = $config["misc"]["https_port"];
                }
            } else {
                if (!empty($config["misc"]["port"])) {
                    $port = $config["misc"]["port"];
                }
            }
        }

        $object->add('port', 'integer');
        $object->set('port', intval($port));

        return $object->getAssoc();
    }

    public function setSettings($params, $context)
    {
        $this->validateMethodContext($context, ['role' => OMV_ROLE_ADMINISTRATOR]);
        $this->validateMethodParams($params, 'rpc.sabnzbd.setsettings');

        $object = $this->database->get($this->dataModelPath);
        $object->setAssoc($params);

        $this->database->set($object);
        $this->dispatcher->notify(OMV_NOTIFY_MODIFY, $this->eventMessagePath, $object);

        return $object->getAssoc();
    }


    public function doBackup($params, $context)
    {
        $this->validateMethodContext($context, ['role' => OMV_ROLE_ADMINISTRATOR]);

        $db = \OMV\Config\Database::getInstance();

        $deviceDir = $db->getAssoc('conf.system.sharedfolder', $params['sharedfolderref']);
        $deviceDir = $db->getAssoc('conf.system.filesystem.mountpoint', $deviceDir['mntentref'])['dir'] . '/' . $deviceDir['reldirpath'];

        $settings = $db->getAssoc($this->dataModelPath);



        // Sabnzbd needs to have run once to generate its
        // settings. Check if this is true
        if (!file_exists($this->sabnzbdSettingsFile)) {
            throw new Exception('No settings folder found!');
        }

        // Create a background process.
        $bgStatusFilename = $this->createBgProcStatus();
        $pid = $this->fork();

        // Parent process.
        if ($pid > 0) {
            $this->initializeBgProcStatus($bgStatusFilename, $pid);
            return $bgStatusFilename;
        }

        // Stop service before backup.
        if (file_exists($this->sabnzbd2SettingsFile)) {
            $cmd = "service sabnzbd2 stop";
            exec($cmd);
        }

        // Get the Sickbeard module
        //$moduleMgr = &OMVModuleMgr::getInstance();
        //$sabnzbdModule = $moduleMgr->getModule($this->getName());
        $cmd = "service sabnzbd stop";
        exec($cmd);
        // Child process.
        try {
            $bgOutputFilename = $this->createBgProcOutput();
            $this->updateBgProcStatus(
                $bgStatusFilename,
                "outputfilename",
                $bgOutputFilename
            );

            $backupPath = sprintf(
                "%s%ssabnzbd.tar.gz",
                $deviceDir,
                DIRECTORY_SEPARATOR
            );

            // Check for other instance.
            if (file_exists($this->sickbeard2SettingsFile)) {
                $cmd = sprintf(
                    "tar -zcvPf %s %s 2>&1",
                    $backupPath,
                    $this->sabnzbdSettingsDir . " " . $this->sabnzbd2SettingsDir
                );
            }else{
                $cmd = sprintf(
                    "tar -zcvPf %s %s 2>&1",
                    $backupPath,
                    $this->sabnzbdSettingsDir
                );
            }

            $this->exec($cmd, $output, $bgOutputFilename);
            $this->finalizeBgProcStatus($bgStatusFilename, $output);
            //$sabnzbdModule->startService();
            // Restart if enabled.
            if ($settings['enable'] = "1") {
                $cmd = "service sickbeard start";
                exec($cmd, $output);
            }
            if ($settings['newinstenable'] = "1") {
                $cmd = "service sickbeard2 start";
                exec($cmd, $output);
            }
            exit(0);
        } catch (Exception $e) {
            $sabnzbdModule->startService();
            $this->finalizeBgProcStatus($bgStatusFilename, "", $e);
            // Restart if enabled.
            if ($settings['enable'] = "1") {
                $cmd = "service sickbeard start";
                exec($cmd, $output);
            }
            if ($settings['newinstenable'] = "1") {
                $cmd = "service sickbeard2 start";
                exec($cmd, $output);
            }
            exit(1);
        }
    }

    public function doRestore($params, $context)
    {
        $this->validateMethodContext($context, ['role' => OMV_ROLE_ADMINISTRATOR]);

        $db = \OMV\Config\Database::getInstance();

        $deviceDir = $db->getAssoc('conf.system.sharedfolder', $params['sharedfolderref']);
        $deviceDir = $db->getAssoc('conf.system.filesystem.mountpoint', $deviceDir['mntentref'])['dir'] . '/' . $deviceDir['reldirpath'];

        $settings = $db->getAssoc($this->dataModelPath);

        $backupPath = sprintf(
            "%s%ssabnzbd.tar.gz",
            $deviceDir,
            DIRECTORY_SEPARATOR
        );

        // Check if backup is present.
        if (file_exists($backupPath)) {
            // Create a background process.
            $bgStatusFilename = $this->createBgProcStatus();
            $pid = $this->fork();

            // Parent process.
            if ($pid > 0) {
                $this->initializeBgProcStatus($bgStatusFilename, $pid);
                return $bgStatusFilename;
            }

            // Delete current settings folder
            rmdir($this->sabnzbdSettingsDir);

            // Delete current settings2 folder. Need to find a way of checking the archive to be sure settings2 is there
            if (file_exists($this->sabnzbd2SettingsFile)) {
                rmdir($this->sabnzbd2SettingsDir);
                $cmd = "/etc/init.d/sabnzbd2 stop";
                $this->exec($cmd);
            }

            // Get the Sabnzbd module
            //$moduleMgr = &OMVModuleMgr::getInstance();
            //$sabnzbdModule = $moduleMgr->getModule($this->getName());
            $cmd = "service sabnzbd stop";
            exec($cmd);

            // Child process.
            try {
                //$sabnzbdModule->stopService();
                $bgOutputFilename = $this->createBgProcOutput();
                $this->updateBgProcStatus(
                    $bgStatusFilename,
                    "outputfilename",
                    $bgOutputFilename
                );

                $cmd = sprintf("tar -zxvf %s -C / 2>&1", $backupPath);

                $this->exec($cmd, $output, $bgOutputFilename);
                $this->finalizeBgProcStatus($bgStatusFilename, $output);
                //$sabnzbdModule->startService();
                exit(0);
            } catch (Exception $e) {
                $this->finalizeBgProcStatus($bgStatusFilename, "", $e);
                //$sabnzbdModule->startService();
                // Restart if enabled.
                if ($settings['enable'] = "1") {
                    $cmd = "service sickbeard start";
                    exec($cmd, $output);
                }
                if ($settings['newinstenable'] = "1") {
                    $cmd = "service sickbeard2 start";
                    exec($cmd, $output);
                }
                exit(1);
            }
        } else {
            throw new Exception('No backup found!');
        }
    }

    public function doUpdateSAB($params, $context)
    {
        $cmd = 'omv-mkconf sabnzbd update 2>&1';

        if (0 !== $this->exec($cmd, $output)) {
            throw new Exception($cmd, implode("\n", $output));
        }

        // Send a modify signal so that the user can trigger a restart.
        $this->dispatcher->notify(OMV_NOTIFY_MODIFY, $this->eventMessagePath);
    }
}
