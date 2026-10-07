"""Compiler-free observer source guard; not an engine or memory runtime test."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT/'entity_lifecycle_observer.h').read_text()

class ObserverContract(unittest.TestCase):
    def test_bindings_are_external_without_numeric_hook_fallbacks(self):
        abi = (ROOT/'entity_lifecycle_abi.h').read_text()
        self.assertNotIn('kAddPattern{{',abi)
        self.assertNotIn('kRemovePattern{{',abi)
        self.assertNotRegex(abi,r'HookSlot\s*=\s*\d+')
        self.assertIn('gamedata.integer(kEntityAddSlotKey)',SOURCE)
        self.assertIn('FleetGamedata::pattern(kEntityAddPatternKey)',SOURCE)
        self.assertIn('addSlot < 0 || removeSlot < 0',SOURCE)
        self.assertTrue((ROOT/'config/entity_lifecycle.gamedata.fragment.ini').is_file())
    def test_raw_handle_public_parent_and_shared_spawn(self):
        self.assertIn('Virtual<CEntitySystem, void, CEntityInstance*, std::uint32_t>',SOURCE)
        self.assertNotIn('Virtual<CEntitySystem, void, CEntityInstance*, CEntityHandle>',SOURCE)
        self.assertIn('&CEntitySystem::OnEntityParentChanged, this, &EntityLifecycleObserver::ParentChanged, nullptr',SOURCE)
        self.assertIn('EntitySpawnObserver spawn_',SOURCE)
        self.assertIsNone(re.search(r'\b(?:AddListenerEntity|RemoveListenerEntity)\s*\(',SOURCE))
        self.assertNotIn('m_entityListeners',SOURCE)

    def test_bind_and_clear_cover_every_owned_hook(self):
        clear = SOURCE[SOURCE.index('bool Clear()'):SOURCE.index('bool Bind(')]
        self.assertLess(clear.index('Busy()'),clear.index('spawn_.Clear()'))
        for name in ('added_','removed_','parent_'):
            self.assertIn(name+'.reset()',clear)
        bind = SOURCE[SOURCE.index('bool Bind('):]
        self.assertLess(bind.index('ValidateEntityHookAbi'),bind.index('std::make_unique<RemoveHook>'))
        self.assertIn('FindOriginalVirtual(table, slot)',bind)
        self.assertIn('added_->AddVtable(table) && removed_->AddVtable(table) && parent_->AddInstance(system) && spawn_.Bind(system, listener)',bind)
        self.assertIn('Clear(); return false;',bind)

    def test_callbacks_have_scopes_and_no_disk_network_or_world_scan(self):
        for name in ('Created','Removing','ParentChanged'):
            match = re.search(r'\b'+name+r'\([^;{]*\)\s*\{(.*?)return \{KHook::Action::Ignore\};',SOURCE,re.S)
            self.assertIsNotNone(match)
            callback = match.group(1)
            self.assertIn('Scope scope(callbacks_)',callback)
            self.assertIn('system == system_',callback)
            self.assertIsNone(re.search(r'\b(?:fopen|ifstream|system|curl|sleep|for|while)\s*\(',callback))

if __name__ == '__main__': unittest.main()
