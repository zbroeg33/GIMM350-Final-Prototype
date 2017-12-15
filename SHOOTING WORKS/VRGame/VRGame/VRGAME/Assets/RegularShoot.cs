using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class RegularShoot : MonoBehaviour {

    public float damage = 25f;
    public float range = 100f;

    public Camera fpsCam;
	
	// Update is called once per frame
	void Update () {
        Debug.Log("update");
  
        if (Input.GetButtonDown("Fire1"))
        {
            
            Shoot();
            Debug.Log("FIRE");
            
        }
    }

    void Shoot()
    {
       
        RaycastHit hit;
      if( Physics.Raycast(fpsCam.transform.position, fpsCam.transform.forward, out hit, range))
        {
            Debug.Log(hit.transform.name);
           Enemy enemy = hit.transform.GetComponent<Enemy>();

            if(enemy != null)
            {
                enemy.TakeDamage(damage);
            }
        }
    }
   
   
}
